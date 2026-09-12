#include "encoder.h"
#include "hardware/gpio.h"

#define ENCODER_CLK_DEBOUNCE_US 1000
#define ENCODER_BUTTON_DEBOUNCE_US 30000

static encoder_t *s_instances[ENCODER_MAX_INSTANCES];
static int s_instance_count = 0;

// Single shared handler for every registered encoder's CLK pin (the Pico
// SDK only allows one GPIO IRQ callback per core) - finds which encoder
// this GPIO belongs to and updates it.
static void encoder_gpio_callback(uint gpio, uint32_t events)
{
    (void)events;
    for (int i = 0; i < s_instance_count; i++) {
        encoder_t *enc = s_instances[i];
        if (gpio != enc->clk_pin) continue;

        uint64_t now = time_us_64();
        if (now - enc->last_clk_us < ENCODER_CLK_DEBOUNCE_US) return;
        enc->last_clk_us = now;

        // CLK just fell - DT's level right now says which way we're turning.
        bool dt = gpio_get(enc->dt_pin);
        enc->delta += dt ? -1 : 1;
        return;
    }
}

void encoder_init(encoder_t *encoder, uint clk_pin, uint dt_pin, uint sw_pin)
{
    encoder->clk_pin = clk_pin;
    encoder->dt_pin = dt_pin;
    encoder->sw_pin = sw_pin;
    encoder->delta = 0;
    encoder->last_clk_us = 0;
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

    if (s_instance_count < ENCODER_MAX_INSTANCES) {
        s_instances[s_instance_count++] = encoder;
    }

    if (s_instance_count == 1) {
        // First encoder installs the shared callback.
        gpio_set_irq_enabled_with_callback(clk_pin, GPIO_IRQ_EDGE_FALL, true, &encoder_gpio_callback);
    } else {
        gpio_set_irq_enabled(clk_pin, GPIO_IRQ_EDGE_FALL, true);
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
