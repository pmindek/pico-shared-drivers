#include "joystick_hold.h"
#include "pico/time.h"

void joystick_hold_init(joystick_hold_t *hold, float threshold, float release_threshold, uint64_t hold_us) {
    hold->threshold = threshold;
    hold->release_threshold = release_threshold;
    hold->hold_us = hold_us;
    hold->held_since_us = 0;
    hold->held_direction = 0;
    hold->fired = false;
}

int joystick_hold_update(joystick_hold_t *hold, float axis_value) {
    uint64_t now = time_us_64();

    if (hold->held_direction != 0) {
        // Mid-hold (or just fired) - stays alive as long as the axis
        // hasn't dropped below release_threshold in that same direction.
        // A brief dip that stays above release_threshold doesn't restart
        // the timer - see the header's own comment on why.
        bool still_past_release = (hold->held_direction > 0 && axis_value >= hold->release_threshold) ||
                                   (hold->held_direction < 0 && axis_value <= -hold->release_threshold);
        if (!still_past_release) {
            hold->held_direction = 0;
            hold->held_since_us = 0;
            hold->fired = false;
            return 0;
        }
        if (!hold->fired && now - hold->held_since_us >= hold->hold_us) {
            hold->fired = true;
            return hold->held_direction;
        }
        return 0;
    }

    // Not currently mid-hold - starting a new one needs the full
    // threshold (not just release_threshold), a deliberate push.
    if (axis_value >= hold->threshold) {
        hold->held_direction = 1;
        hold->held_since_us = now;
        hold->fired = false;
    } else if (axis_value <= -hold->threshold) {
        hold->held_direction = -1;
        hold->held_since_us = now;
        hold->fired = false;
    }
    return 0;
}
