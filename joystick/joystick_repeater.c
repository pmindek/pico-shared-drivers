#include "joystick_repeater.h"
#include "pico/time.h"
#include <math.h>

void joystick_repeater_init(joystick_repeater_t *repeater, joystick_t *joystick, float threshold,
                             float release_threshold, float min_steps_per_sec, float max_steps_per_sec,
                             float curve_power) {
    repeater->joystick = joystick;
    repeater->threshold = threshold;
    repeater->release_threshold = release_threshold;
    repeater->min_steps_per_sec = min_steps_per_sec;
    repeater->max_steps_per_sec = max_steps_per_sec;
    repeater->curve_power = curve_power;
    repeater->x_due_us = 0;
    repeater->y_due_us = 0;
    repeater->x_armed = true;
    repeater->y_armed = true;
}

// One axis' worth of the repeat state machine - shared by both X and Y in
// joystick_repeater_update() below. `due_us`/`armed` are that axis' own
// x_due_us/x_armed (or y_) fields.
static int step_axis(joystick_repeater_t *repeater, uint64_t *due_us, bool *armed, uint64_t now, float axis_value) {
    float magnitude = fabsf(axis_value);

    // Re-arms once the axis has genuinely withdrawn, independent of
    // whether it's currently repeating - see the header's own comment on
    // why this (not just "below threshold") is what actually prevents a
    // moment of wobble near threshold from granting more than one
    // instant step.
    if (magnitude < repeater->release_threshold) *armed = true;

    if (magnitude < repeater->threshold) {
        // Below threshold - not pushed, as far as repeating goes.
        // Resetting to 0 means the next push starts a fresh cycle, not
        // mid-interval - `armed` is separate and NOT reset here, since a
        // push that dips just under threshold and immediately back over
        // (without ever reaching release_threshold) must not get another
        // free instant step.
        *due_us = 0;
        return 0;
    }
    if (magnitude > 1.0f) magnitude = 1.0f;

    float rate = repeater->min_steps_per_sec +
                 (repeater->max_steps_per_sec - repeater->min_steps_per_sec) * powf(magnitude, repeater->curve_power);
    if (rate < 0.001f) rate = 0.001f; // guard a pathological (e.g. zero) min_steps_per_sec against a divide-by-zero below
    uint64_t interval_us = (uint64_t)(1000000.0f / rate);

    int direction = axis_value > 0.0f ? 1 : -1;

    if (*due_us == 0) {
        // Just started repeating - schedule the next tick either way,
        // but only actually fire an instant step if this is a genuine
        // new push (armed), not just the same wobble re-crossing
        // threshold.
        *due_us = now + interval_us;
        if (*armed) {
            *armed = false;
            return direction;
        }
        return 0;
    }

    int steps = 0;
    while (now >= *due_us) {
        steps += direction;
        *due_us += interval_us;
    }
    return steps;
}

void joystick_repeater_update(joystick_repeater_t *repeater, int *dx, int *dy) {
    float x, y;
    joystick_read(repeater->joystick, &x, &y);
    uint64_t now = time_us_64();

    *dx = step_axis(repeater, &repeater->x_due_us, &repeater->x_armed, now, x);
    *dy = step_axis(repeater, &repeater->y_due_us, &repeater->y_armed, now, y);
}
