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

    if (*due_us == 0) {
        // Just crossed the deadzone - schedule the first step one full
        // interval from now, same as every step after it. An earlier
        // version fired this one immediately (felt more responsive, like
        // a keyboard's own key-repeat firing its first character right
        // away) but that meant every single 0->nonzero edge granted a
        // free step with no time requirement at all - harmless for a
        // deliberate push, but a joystick resting right at the deadzone
        // boundary can flicker across it from ADC/pot noise alone (the
        // same kind of noise this project fought with the capacitive
        // touch sensor), and each flicker was its own free step - fast
        // enough in a row to send a cursor flying to a corner with
        // nobody touching the stick. Requiring the full interval to
        // actually elapse means a single-poll noise blip contributes
        // nothing: it resets back to *due_us = 0 (see above) before
        // "now >= *due_us" can ever become true.
        *due_us = now + interval_us;
        return 0;
    }

    int direction = axis_value > 0.0f ? 1 : -1;
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
