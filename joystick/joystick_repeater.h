#pragma once

#include "joystick.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Turns a joystick's continuous analog position into a stream of
// discrete, auto-repeating "step" events per axis - e.g. moving a
// grid/list cursor left/right/up/down - the same idea as a keyboard's
// key-repeat, but with the repeat RATE itself driven by how far the
// stick is pushed, rather than one fixed rate: barely past the
// joystick's own deadzone, steps come slowly (min_steps_per_sec); pushed
// all the way to an edge, they come quickly (max_steps_per_sec). How
// deflection maps onto that range isn't necessarily linear - see
// curve_power below.
//
// X and Y repeat independently of each other (tracked separately), so
// e.g. holding the stick diagonally can step a cursor both horizontally
// and vertically at once, each at its own rate.
typedef struct {
    joystick_t *joystick;
    float min_steps_per_sec; // repeat rate just past the joystick's own deadzone
    float max_steps_per_sec; // repeat rate at full deflection (|axis| == 1.0)
    float curve_power;       // deflection -> rate curve: 1.0 = linear, 2.0 = quadratic (stays
                              // slow for most of the range, only ramping up fast near full
                              // deflection), 3.0 = even more back-loaded, etc.
    uint64_t x_due_us, y_due_us; // time_us64() of each axis's next step, or 0 = "not currently repeating"
} joystick_repeater_t;

// `joystick` must already be initialized (see joystick_init()) and stay
// valid for as long as `repeater` is used - its address is kept, not
// copied. min_steps_per_sec/max_steps_per_sec/curve_power are stored as
// given - see their own comments above for what they mean; e.g. (1.0f,
// 10.0f, 2.0f) is "1 step/sec just past center, ramping quadratically up
// to 10 steps/sec at full deflection".
void joystick_repeater_init(joystick_repeater_t *repeater, joystick_t *joystick, float min_steps_per_sec,
                             float max_steps_per_sec, float curve_power);

// Call once per main-loop iteration (or at least as often as you're
// willing to react to a step). Reads the joystick fresh and writes how
// many steps to apply on each axis since the last call to *dx/*dy -
// typically 0 on a given call, occasionally +-1, or (rarely, only if
// polled unusually slowly relative to the current repeat rate) more than
// 1 in one call, since a slow poller still gets credit for every step it
// missed rather than just one. Centering the stick (back inside the
// deadzone) resets that axis so the next push starts a fresh repeat
// cycle, not mid-interval.
void joystick_repeater_update(joystick_repeater_t *repeater, int *dx, int *dy);

#ifdef __cplusplus
}
#endif
