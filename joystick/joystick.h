#pragma once

#include "pico/stdlib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Analog 2-axis joystick (the common "PS2-style" breakout module: two
// potentiometers, one per axis, plus a momentary push button under the
// stick itself) - X and Y read over the RP2040/RP2350's ADC; the button
// is debounced the same way as button.h/encoder.h's own SW pins (that
// logic is duplicated here, not shared via #include - matching this
// repo's convention of each driver directory being self-contained).
//
// x_gpio/y_gpio must be ADC-capable pins (GP26-GP29 on a Pico/Pico 2 -
// ADC0-ADC3); sw_pin is a plain GPIO, active-low with an internal
// pull-up, same wiring convention as button.h/encoder.h's own SW pins.
// Polled, not interrupt-driven: call joystick_read()/
// joystick_button_pressed() once per main-loop iteration.
//
// joystick_init() calibrates each axis' rest/center reading itself -
// leave the stick untouched while it runs (called once, e.g. from
// hardware_init(), same as an IMU's own zero-rate calibration) - a raw
// potentiometer's true center is rarely exactly half of the ADC's full
// range, so this is measured rather than assumed.
//
// joystick_read() normalizes both axes to -1.0..+1.0 (0.0 = centered),
// with a small deadzone around center to absorb ADC noise/pot wander
// when the stick is untouched but not perfectly still - without it, a UI
// driven directly off this would visibly jitter at rest. If an axis
// reads backwards on real hardware, either swap x_gpio/y_gpio at the
// call site or negate the value the caller gets back - same "verify
// against real hardware, fix at the call site" convention as encoder.h's
// own CW/CCW direction.

typedef struct {
    uint x_adc_input; // 0-3 - adc_select_input()'s argument, derived from x_gpio at init
    uint y_adc_input;
    uint16_t center_x; // raw 12-bit ADC reading at rest, captured by joystick_init()
    uint16_t center_y;
    uint sw_pin;
    bool sw_was_pressed;
    uint64_t sw_last_change_us;
} joystick_t;

// Initializes the ADC (safe to call more than once across multiple
// joystick_init() calls - see joystick.c) and both axis pins, sets up
// sw_pin (input, pull-up), then calibrates center_x/center_y - see the
// module comment above. `joystick` must stay valid for as long as the
// joystick is used - its address is kept, not copied.
void joystick_init(joystick_t *joystick, uint x_gpio, uint y_gpio, uint sw_pin);

// Reads both axes fresh (no caching - call as often as you want) and
// writes normalized -1.0..+1.0 values (0.0 = centered) to *x/*y.
void joystick_read(joystick_t *joystick, float *x, float *y);

// True once per physical press of the joystick's own button (on the press
// edge, debounced), not while held.
bool joystick_button_pressed(joystick_t *joystick);

#ifdef __cplusplus
}
#endif
