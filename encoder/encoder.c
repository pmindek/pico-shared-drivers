#include "encoder.h"
#include "hardware/gpio.h"

#define ENCODER_BUTTON_DEBOUNCE_US 30000

// Full 4-step quadrature decode. States are (CLK<<1 | DT): 3 = rest (both
// high), 1/0/2 = the three transitional states. Valid sequences:
//   CW:  3 -> 1 -> 0 -> 2 -> 3
//   CCW: 3 -> 2 -> 0 -> 1 -> 3
// table[(old_state << 2) | new_state] gives the direction that single
// transition contributes (+1/-1), or 0 if that (old, new) pair can't occur
// in either clean sequence (i.e. a state got skipped - contact bounce or a
// missed interrupt) - treated as noise, not counted.
static const int8_t kQuadratureTable[16] = {
    /* 0,0 */ 0, /* 0,1 */ -1, /* 0,2 */ +1, /* 0,3 */ 0,
    /* 1,0 */ +1, /* 1,1 */ 0, /* 1,2 */ 0, /* 1,3 */ -1,
    /* 2,0 */ -1, /* 2,1 */ 0, /* 2,2 */ 0, /* 2,3 */ +1,
    /* 3,0 */ 0, /* 3,1 */ +1, /* 3,2 */ -1, /* 3,3 */ 0,
};

static encoder_t *s_instances[ENCODER_MAX_INSTANCES];
static int s_instance_count = 0;

static inline uint8_t read_pin_state(const encoder_t *enc)
{
    return (uint8_t)((gpio_get(enc->clk_pin) << 1) | gpio_get(enc->dt_pin));
}

static void encoder_update(encoder_t *enc)
{
    uint8_t new_state = read_pin_state(enc);
    if (new_state == enc->pin_state) return; // no real change (the other pin's IRQ already handled it)

    enc->last_rotation_us = time_us_64(); // any real edge counts as activity, not just a completed click

    int8_t step = kQuadratureTable[(enc->pin_state << 2) | new_state];
    enc->pin_state = new_state;

    if (step == 0) {
        // Impossible transition (a state got skipped) - resync silently
        // rather than let it corrupt an in-progress count.
        enc->accum = 0;
        return;
    }

    enc->accum += step;

    if (new_state == 3) { // back at rest
        if (enc->accum == 4) {
            enc->delta += 1;
        } else if (enc->accum == -4) {
            enc->delta -= 1;
        }
        // Any other accumulated value means an interrupted/bounced cycle -
        // discarded rather than guessing at a click that may not have
        // happened.
        enc->accum = 0;
    }
}

// Single shared handler for every registered encoder's CLK/DT pins (the
// Pico SDK only allows one GPIO IRQ callback per core).
static void encoder_gpio_callback(uint gpio, uint32_t events)
{
    (void)events;
    for (int i = 0; i < s_instance_count; i++) {
        encoder_t *enc = s_instances[i];
        if (gpio == enc->clk_pin || gpio == enc->dt_pin) {
            encoder_update(enc);
            return;
        }
    }
}

void encoder_init(encoder_t *encoder, uint clk_pin, uint dt_pin, uint sw_pin)
{
    encoder->clk_pin = clk_pin;
    encoder->dt_pin = dt_pin;
    encoder->sw_pin = sw_pin;
    encoder->accum = 0;
    encoder->delta = 0;
    encoder->last_rotation_us = 0;
    encoder->sw_was_pressed = false;
    encoder->sw_last_change_us = 0;

    gpio_init(clk_pin);
    gpio_set_dir(clk_pin, GPIO_IN);
    gpio_pull_up(clk_pin);

    gpio_init(dt_pin);
    gpio_set_dir(dt_pin, GPIO_IN);
    gpio_pull_up(dt_pin);

    gpio_init(sw_pin);
    gpio_set_dir(sw_pin, GPIO_IN);
    gpio_pull_up(sw_pin); // button should connect the pin to GND when pressed

    encoder->pin_state = read_pin_state(encoder);

    if (s_instance_count < ENCODER_MAX_INSTANCES) {
        s_instances[s_instance_count++] = encoder;
    }

    const uint32_t events = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
    if (s_instance_count == 1) {
        // First encoder installs the shared callback.
        gpio_set_irq_enabled_with_callback(clk_pin, events, true, &encoder_gpio_callback);
        gpio_set_irq_enabled(dt_pin, events, true);
    } else {
        gpio_set_irq_enabled(clk_pin, events, true);
        gpio_set_irq_enabled(dt_pin, events, true);
    }
}

int encoder_take_delta(encoder_t *encoder)
{
    // Not atomic against the IRQ touching `delta` mid-read, but the
    // subtract-what-we-read (rather than a plain reset to 0) means a click
    // landing between the read and the subtract still ends up counted, not
    // lost - fine for a UI control, no need for a critical section here.
    int delta = encoder->delta;
    encoder->delta -= delta;
    return delta;
}

bool encoder_button_pressed(encoder_t *encoder)
{
    bool is_pressed = !gpio_get(encoder->sw_pin); // active low
    uint64_t now = time_us_64();

    bool triggered = false;
    if (is_pressed != encoder->sw_was_pressed && (now - encoder->sw_last_change_us) > ENCODER_BUTTON_DEBOUNCE_US) {
        encoder->sw_last_change_us = now;
        encoder->sw_was_pressed = is_pressed;
        triggered = is_pressed;
    }
    return triggered;
}

uint64_t encoder_get_last_activity_us(const encoder_t *encoder)
{
    uint64_t rotation = encoder->last_rotation_us;
    uint64_t button = encoder->sw_last_change_us;
    return rotation > button ? rotation : button;
}
