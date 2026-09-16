#include "joystick.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

#define JOYSTICK_ADC_GPIO_BASE 26 // GP26=ADC0, GP27=ADC1, GP28=ADC2, GP29=ADC3 on RP2040/RP2350

#define JOYSTICK_ADC_MAX 4095 // 12-bit ADC, same on RP2040 and RP2350
#define JOYSTICK_DEADZONE 40  // raw ADC counts either side of center treated as exactly 0 - absorbs pot/ADC noise at rest

#define JOYSTICK_CALIBRATION_SAMPLES 32

#define JOYSTICK_BUTTON_DEBOUNCE_US 30000 // same value as button.h/encoder.h's own SW debounce

// adc_init() resets/configures the whole ADC block, not just one channel.
// Harmless to call again (it just resets to the same state), but
// pointless if it's already running - guarded so a second
// joystick_init() call (e.g. a future second joystick) doesn't re-reset
// an already-configured ADC mid-session for no reason.
static bool s_adc_initialized = false;

static uint16_t read_raw(uint adc_input)
{
    adc_select_input(adc_input);
    return adc_read();
}

static float normalize(uint16_t raw, uint16_t center)
{
    int32_t delta = (int32_t)raw - (int32_t)center;
    if (delta > -JOYSTICK_DEADZONE && delta < JOYSTICK_DEADZONE) return 0.0f;

    // Scale against whichever half-range this delta's sign is on - center
    // isn't necessarily exactly JOYSTICK_ADC_MAX/2, so the true distance to
    // each end can genuinely differ.
    float range = delta > 0 ? (float)(JOYSTICK_ADC_MAX - center) : (float)center;
    if (range < 1.0f) range = 1.0f; // guard a center pinned at an extreme (shouldn't happen with a real pot)

    float normalized = (float)delta / range;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;
    return normalized;
}

void joystick_init(joystick_t *joystick, uint x_gpio, uint y_gpio, uint sw_pin)
{
    if (!s_adc_initialized) {
        adc_init();
        s_adc_initialized = true;
    }

    adc_gpio_init(x_gpio);
    adc_gpio_init(y_gpio);
    joystick->x_adc_input = x_gpio - JOYSTICK_ADC_GPIO_BASE;
    joystick->y_adc_input = y_gpio - JOYSTICK_ADC_GPIO_BASE;

    joystick->sw_pin = sw_pin;
    joystick->sw_was_pressed = false;
    joystick->sw_last_change_us = 0;
    gpio_init(sw_pin);
    gpio_set_dir(sw_pin, GPIO_IN);
    gpio_pull_up(sw_pin); // button should connect the pin to GND when pressed

    // Center calibration: average a handful of samples rather than trust a
    // single noisy read - same reasoning as an IMU's own gyro calibration,
    // just far fewer samples needed since a resting potentiometer is far
    // more stable than a held-still gyro.
    uint32_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < JOYSTICK_CALIBRATION_SAMPLES; i++) {
        sum_x += read_raw(joystick->x_adc_input);
        sum_y += read_raw(joystick->y_adc_input);
    }
    joystick->center_x = (uint16_t)(sum_x / JOYSTICK_CALIBRATION_SAMPLES);
    joystick->center_y = (uint16_t)(sum_y / JOYSTICK_CALIBRATION_SAMPLES);
}

void joystick_read(joystick_t *joystick, float *x, float *y)
{
    *x = normalize(read_raw(joystick->x_adc_input), joystick->center_x);
    *y = normalize(read_raw(joystick->y_adc_input), joystick->center_y);
}

bool joystick_button_pressed(joystick_t *joystick)
{
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
