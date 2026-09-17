#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Detects a sustained directional hold on a single joystick axis value -
// e.g. holding the stick left/right (or up/down) past a threshold for at
// least `hold_us` continuously - firing once when that duration is
// reached, not every tick while still held. A one-shot "long press" on
// an axis, a separate gesture from joystick_repeater.h's own
// auto-repeating steps (which is about a rate of discrete steps, not a
// single timed hold).
//
// Uses the same two-threshold hysteresis as joystick_repeater.h, for the
// same reason: starting a hold needs a deliberate push past `threshold`,
// but a brief noise dip during the hold_us wait shouldn't cancel and
// restart the timer from scratch just because it grazed under that same
// value - only dropping below the lower `release_threshold` actually
// cancels an in-progress hold.
typedef struct {
    float threshold;         // minimum |axis| to START a new hold
    float release_threshold; // hold continues as long as |axis| stays above this (lower than threshold); dropping below it cancels
    uint64_t hold_us;        // how long past threshold before firing
    uint64_t held_since_us;  // 0 = not currently mid-hold
    int held_direction;      // 0 = none, else the sign (+1/-1) of the direction currently being held
    bool fired;               // already fired for this hold - suppresses firing again every tick while still held
} joystick_hold_t;

// threshold/release_threshold/hold_us are stored as given - see the
// struct's own comments for what they mean. release_threshold must be
// meaningfully lower than threshold (a real margin, not just "less
// than") for the hysteresis to do its job - e.g. (0.5f, 0.25f, 1000000)
// is "hold past 50% deflection for 1 second, cancelled only by
// dropping back below 25%".
void joystick_hold_init(joystick_hold_t *hold, float threshold, float release_threshold, uint64_t hold_us);

// Call once per tick with the current axis value (e.g. the joystick's
// own x or y from joystick_read() - any -1.0..1.0 value). Returns +1 the
// instant a hold duration is first reached in the positive direction,
// -1 for the negative direction, 0 otherwise (including every tick
// before the duration is reached, and every tick after the one it fires
// on while still held - see release_threshold above for what actually
// re-arms it).
int joystick_hold_update(joystick_hold_t *hold, float axis_value);

#ifdef __cplusplus
}
#endif
