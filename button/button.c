#include "button.h"
#include "hardware/gpio.h"

void button_init(button_t *button, uint pin)
{
    button->pin = pin;
    button->was_pressed = false;
    button->last_change_us = 0;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin); // button should connect the pin to GND when pressed
}

bool button_pressed(button_t *button)
{
    bool is_pressed = !gpio_get(button->pin); // active low
    uint64_t now = time_us_64();

    bool triggered = false;
    if (is_pressed != button->was_pressed && (now - button->last_change_us) > BUTTON_DEBOUNCE_US) {
        button->last_change_us = now;
        button->was_pressed = is_pressed;
        triggered = is_pressed;
    }
    return triggered;
}

uint64_t button_get_last_change_us(const button_t *button)
{
    return button->last_change_us;
}
