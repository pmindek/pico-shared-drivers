#include "joystick.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <math.h>

#define JOYSTICK_ADC_GPIO_BASE 26 // GP26=ADC0, GP27=ADC1, GP28=ADC2, GP29=ADC3 on RP2040/RP2350

#define JOYSTICK_ADC_MAX 4095 // 12-bit, whichever converter produced it - see joystick_axis_read_fn

// Default raw counts either side of center reported as exactly 0. Sized for
// the Pico's own ADC, which wanders tens of counts at rest; an external
// converter is usually far quieter and should set its own with
// joystick_set_deadzone(). Every count of this is travel the player can feel
// doing nothing, so it wants to be as small as the hardware allows - but no
// smaller than the stick's own rest position wanders between releases, which
// is often the real floor rather than converter noise.
#define JOYSTICK_DEADZONE 40

#define JOYSTICK_CALIBRATION_SAMPLES 32

#define JOYSTICK_BUTTON_DEBOUNCE_US 30000 // same value as button.h/encoder.h's own SW debounce

// adc_init() resets/configures the whole ADC block, not just one channel.
// Harmless to call again (it just resets to the same state), but
// pointless if it's already running - guarded so a second
// joystick_init() call (e.g. a future second joystick) doesn't re-reset
// an already-configured ADC mid-session for no reason.
static bool s_adc_initialized = false;

static uint16_t read_raw(const joystick_t *joystick, uint axis)
{
    if (joystick->read_axis) return joystick->read_axis(joystick->read_ctx, (int)axis);
    adc_select_input(axis);
    return adc_read();
}

// Everything below this line is identical for both sources: the external
// path hands back the same 0-4095 scale, so centre, dead zone and curve are
// unaware of where a reading came from.
static void calibrate_center(joystick_t *joystick)
{
    uint32_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < JOYSTICK_CALIBRATION_SAMPLES; i++) {
        sum_x += read_raw(joystick, joystick->x_axis);
        sum_y += read_raw(joystick, joystick->y_axis);
    }
    joystick->center_x = (uint16_t)(sum_x / JOYSTICK_CALIBRATION_SAMPLES);
    joystick->center_y = (uint16_t)(sum_y / JOYSTICK_CALIBRATION_SAMPLES);
}

static void init_common(joystick_t *joystick, uint sw_pin)
{
    joystick->sw_pin = sw_pin;
    joystick->sw_was_pressed = false;
    joystick->sw_last_change_us = 0;
    if (sw_pin != JOYSTICK_NO_SW) {
        gpio_init(sw_pin);
        gpio_set_dir(sw_pin, GPIO_IN);
        gpio_pull_up(sw_pin); // button should connect the pin to GND when pressed
    }

    joystick->deadzone = JOYSTICK_DEADZONE;
    joystick->curve_exponent = 1.0f;
    joystick->suppressed = false;
    joystick->rearm_threshold = 0.0f;
}

// Normalizes to -1.0..+1.0 AND applies the response curve in one step -
// deliberately not two composed functions (a plain normalize() then a
// separate apply_curve()) because the curve needs to know where the
// deadzone boundary sits in normalized terms, not just the -1..1 value.
//
// Why: right at the deadzone edge, a plain normalize()'s "escaped" value
// isn't a true 0 - it's already ~JOYSTICK_DEADZONE/range (a couple of
// percent), since anything smaller is caught by the deadzone check and
// reported as exactly 0.0 instead. A pure power curve (x^exponent)
// applied to that small-but-nonzero value amplifies it: exponent < 1 has
// an unboundedly steep slope right at its own x=0, so ADC/pot noise
// flickering across the deadzone boundary - normally invisible - turns
// into a large, visible jump once curved (worse the lower the exponent),
// even with the stick left completely alone. Fix: remap the post-deadzone
// range so 0.0 falls exactly ON the deadzone boundary (not a couple
// percent past it) before curving - now the worst case (noise sitting
// right at the boundary) remaps to ~0, and 0^exponent is exactly 0 for
// any exponent, so there's nothing left for the curve to amplify.
static float normalize_and_curve(uint16_t raw, uint16_t center, float exponent, uint16_t deadzone)
{
    int32_t delta = (int32_t)raw - (int32_t)center;
    if (delta > -(int32_t)deadzone && delta < (int32_t)deadzone) return 0.0f;

    // Scale against whichever half-range this delta's sign is on - center
    // isn't necessarily exactly JOYSTICK_ADC_MAX/2, so the true distance to
    // each end can genuinely differ.
    float range = delta > 0 ? (float)(JOYSTICK_ADC_MAX - center) : (float)center;
    if (range < 1.0f) range = 1.0f; // guard a center pinned at an extreme (shouldn't happen with a real pot)

    float magnitude = (float)(delta < 0 ? -delta : delta) / range;
    if (magnitude > 1.0f) magnitude = 1.0f;

    float deadzone_edge = (float)deadzone / range;
    if (deadzone_edge > 0.99f) deadzone_edge = 0.99f; // guard a pathologically tiny range

    // Rescale magnitude (guaranteed >= deadzone_edge here) so it spans
    // 0..1 starting exactly at the deadzone boundary, instead of
    // deadzone_edge..1.
    float remapped = (magnitude - deadzone_edge) / (1.0f - deadzone_edge);
    if (remapped < 0.0f) remapped = 0.0f; // guard float rounding right at the boundary
    if (remapped > 1.0f) remapped = 1.0f;

    // Skips powf() entirely at the default exponent (1.0), the
    // overwhelmingly common case, rather than doing (and rounding through)
    // a no-op x^1.0 on every single read.
    float curved = exponent == 1.0f ? remapped : powf(remapped, exponent);
    return delta < 0 ? -curved : curved;
}

void joystick_init(joystick_t *joystick, uint x_gpio, uint y_gpio, uint sw_pin)
{
    if (!s_adc_initialized) {
        adc_init();
        s_adc_initialized = true;
    }

    adc_gpio_init(x_gpio);
    adc_gpio_init(y_gpio);
    joystick->read_axis = NULL;
    joystick->read_ctx = NULL;
    joystick->x_axis = x_gpio - JOYSTICK_ADC_GPIO_BASE;
    joystick->y_axis = y_gpio - JOYSTICK_ADC_GPIO_BASE;

    init_common(joystick, sw_pin);

    // Center calibration: average a handful of samples rather than trust a
    // single noisy read - same reasoning as an IMU's own gyro calibration,
    // just far fewer samples needed since a resting potentiometer is far
    // more stable than a held-still gyro. Whatever position the stick is in
    // right now becomes its zero, so don't let anything rest against it
    // while the board boots.
    calibrate_center(joystick);
}

void joystick_init_source(joystick_t *joystick, joystick_axis_read_fn read, void *ctx, int x_axis, int y_axis,
                          uint sw_pin)
{
    // No adc_init() here: this path doesn't touch the on-chip ADC at all,
    // which is the point - it frees those pins for something else.
    joystick->read_axis = read;
    joystick->read_ctx = ctx;
    joystick->x_axis = (uint)x_axis;
    joystick->y_axis = (uint)y_axis;

    init_common(joystick, sw_pin);

    // Averaging is nearly a no-op here if the source refreshes more slowly
    // than this loop runs - it just averages the same reading several times.
    // Harmless, and the kind of converter worth using this path for is quiet
    // enough that one sample would do.
    calibrate_center(joystick);
}

void joystick_read(joystick_t *joystick, float *x, float *y)
{
    float read_x = normalize_and_curve(read_raw(joystick, joystick->x_axis), joystick->center_x,
                                       joystick->curve_exponent, joystick->deadzone);
    float read_y = normalize_and_curve(read_raw(joystick, joystick->y_axis), joystick->center_y,
                                       joystick->curve_exponent, joystick->deadzone);

    if (joystick->suppressed) {
        if (fabsf(read_x) < joystick->rearm_threshold && fabsf(read_y) < joystick->rearm_threshold) {
            joystick->suppressed = false; // back at rest - from this read on, report it as it is
        } else {
            read_x = 0.0f;
            read_y = 0.0f;
        }
    }

    *x = read_x;
    *y = read_y;
}

bool joystick_button_pressed(joystick_t *joystick)
{
    if (joystick->sw_pin == JOYSTICK_NO_SW) return false; // no button wired - see JOYSTICK_NO_SW

    bool is_pressed = !gpio_get(joystick->sw_pin); // active low
    uint64_t now = time_us_64();

    bool triggered = false;
    if (is_pressed != joystick->sw_was_pressed && (now - joystick->sw_last_change_us) > JOYSTICK_BUTTON_DEBOUNCE_US) {
        joystick->sw_last_change_us = now;
        joystick->sw_was_pressed = is_pressed;
        triggered = is_pressed;
    }
    return triggered;
}

void joystick_suppress_until_centered(joystick_t *joystick, float threshold)
{
    joystick->rearm_threshold = threshold;
    joystick->suppressed = true;
}

void joystick_set_curve_exponent(joystick_t *joystick, float exponent)
{
    joystick->curve_exponent = exponent;
}

float joystick_get_curve_exponent(const joystick_t *joystick)
{
    return joystick->curve_exponent;
}

void joystick_set_deadzone(joystick_t *joystick, uint16_t counts)
{
    joystick->deadzone = counts;
}

uint16_t joystick_get_deadzone(const joystick_t *joystick)
{
    return joystick->deadzone;
}
