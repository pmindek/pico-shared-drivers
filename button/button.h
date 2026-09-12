#pragma once

#include "pico/stdlib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Debounced momentary push button - active low, internal pull-up, same
// wiring convention as the encoder driver's SW pin (button connects the pin
// to GND when pressed). Polled, not interrupt-driven: call
// button_pressed() once per main-loop iteration.

#define BUTTON_DEBOUNCE_US 30000

typedef struct {
    uint pin;
    bool was_pressed;
    uint64_t last_change_us;
} button_t;

// Initializes the GPIO (input, pull-up). `button` must stay valid for as
// long as the button is used - its address is kept, not copied.
void button_init(button_t *button, uint pin);

// True once per physical press (on the press edge, debounced), not while
// held.
bool button_pressed(button_t *button);

// Timestamp (time_us_64()) of the most recent debounced state change
// (press or release) - unlike button_pressed(), reading this has no side
// effects, so it's safe to poll from code that isn't the "owner" consuming
// actual press events (e.g. an idle-timeout that just wants to know "did
// anything happen recently").
uint64_t button_get_last_change_us(const button_t *button);

#ifdef __cplusplus
}
#endif
