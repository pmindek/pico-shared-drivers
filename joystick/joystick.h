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
//
// A response-curve exponent is then applied to both axes (see
// joystick_set_curve_exponent()) - sign-preserving, output = sign(x) *
// |x|^exponent, 1.0 (the default) = linear/no curve. The curve is applied
// to the post-deadzone range remapped so 0.0 sits exactly at the deadzone
// boundary (not the small-but-nonzero raw escape value) - otherwise an
// exponent < 1 would amplify ordinary noise flickering across that
// boundary into a large jump, since x^exponent has an unboundedly steep
// slope near its own x=0 (see joystick.c's normalize_and_curve()). It's
// part of joystick_t (not separate global state) so a second joystick
// instance could someday have its own curve, but in practice every screen
// shares one joystick_t (hardware_get_joystick()), so setting it from any
// one of them - e.g. a calibration screen - is immediately visible to
// every other screen's joystick_read()/joystick_repeater_update() calls
// too, and stays that way until changed again (not reset when switching
// screens).

typedef struct {
    uint x_adc_input; // 0-3 - adc_select_input()'s argument, derived from x_gpio at init
    uint y_adc_input;
    uint16_t center_x; // raw 12-bit ADC reading at rest, captured by joystick_init()
    uint16_t center_y;
    uint sw_pin;
    bool sw_was_pressed;
    uint64_t sw_last_change_us;
    float curve_exponent; // see joystick_set_curve_exponent()
    bool suppressed;      // see joystick_suppress_until_centered()
    float rearm_threshold;
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

// Sets the response-curve exponent joystick_read() applies to both axes
// from here on - sign(x) * |x|^exponent. 1.0 = linear (joystick_init()'s
// default); >1.0 = soft near center, sharper near the edges (more
// precision for small movements); <1.0 = the opposite (more sensitivity
// for small movements, flatter near the edges). Not clamped here - a
// pathological exponent (0, negative, huge) is the caller's problem, not
// this driver's to silently second-guess.
void joystick_set_curve_exponent(joystick_t *joystick, float exponent);

// Makes joystick_read() report (0, 0) - centered - until the stick has been
// brought back below `threshold` (|x| and |y| both under it, judged on the
// same curved values joystick_read() returns), then reports normally again
// until the next call. For a UI that has just moved on (e.g. opened a new
// screen by holding the stick in some direction): without this, whatever
// the stick is still doing carries over as if it were a fresh input on the
// new screen (a cursor that immediately runs off, ...). Applies to every
// consumer at once, since they all read through joystick_read() -
// joystick_repeater and joystick_hold included. The button isn't affected.
void joystick_suppress_until_centered(joystick_t *joystick, float threshold);
float joystick_get_curve_exponent(const joystick_t *joystick);

#ifdef __cplusplus
}
#endif
