#include "joystick_repeater.h"
#include "pico/time.h"
#include <math.h>

void joystick_repeater_init(joystick_repeater_t *repeater, joystick_t *joystick, float min_steps_per_sec,
                             float max_steps_per_sec, float curve_power) {
    repeater->joystick = joystick;
    repeater->min_steps_per_sec = min_steps_per_sec;
    repeater->max_steps_per_sec = max_steps_per_sec;
    repeater->curve_power = curve_power;
    repeater->x_due_us = 0;
    repeater->y_due_us = 0;
}

// One axis' worth of the repeat state machine - shared by both X and Y in
// joystick_repeater_update() below. `due_us` is that axis' own
// x_due_us/y_due_us field.
static int step_axis(joystick_repeater_t *repeater, uint64_t *due_us, uint64_t now, float axis_value) {
    if (axis_value == 0.0f) {
        *due_us = 0; // back inside the deadzone - next push starts a fresh repeat cycle
        return 0;
    }

    float magnitude = fabsf(axis_value);
    if (magnitude > 1.0f) magnitude = 1.0f;
    float rate = repeater->min_steps_per_sec +
                 (repeater->max_steps_per_sec - repeater->min_steps_per_sec) * powf(magnitude, repeater->curve_power);
    if (rate < 0.001f) rate = 0.001f; // guard a pathological (e.g. zero) min_steps_per_sec against a divide-by-zero below
    uint64_t interval_us = (uint64_t)(1000000.0f / rate);

    int direction = axis_value > 0.0f ? 1 : -1;

    if (*due_us == 0) {
        // Just crossed the deadzone - fires the first step immediately
        // (feels responsive, the same way a keyboard's own key-repeat
        // fires the first character right away before any delay applies)
        // and schedules the next one from here.
        *due_us = now + interval_us;
        return direction;
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

    *dx = step_axis(repeater, &repeater->x_due_us, now, x);
    *dy = step_axis(repeater, &repeater->y_due_us, now, y);
}
