#include "touch.h"

#include <string.h>

#include "pico/time.h"

void touch_init(touch_t *touch, const touch_backend_t *backend, const touch_orientation_t *orientation,
                int tap_slop_px)
{
    memset(touch, 0, sizeof(*touch));
    touch->backend = *backend;
    touch->orientation = *orientation;
    touch->width = orientation->swap_xy ? orientation->native_height : orientation->native_width;
    touch->height = orientation->swap_xy ? orientation->native_width : orientation->native_height;
    touch->tap_slop_px = tap_slop_px;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Controller coordinates -> screen coordinates (see touch_orientation_t).
static void to_screen(const touch_t *touch, int rx, int ry, int *sx, int *sy)
{
    const touch_orientation_t *o = &touch->orientation;
    int x = o->swap_xy ? ry : rx;
    int y = o->swap_xy ? rx : ry;
    if (o->mirror_x) x = touch->width - 1 - x;
    if (o->mirror_y) y = touch->height - 1 - y;
    *sx = clamp(x, 0, touch->width - 1);
    *sy = clamp(y, 0, touch->height - 1);
}

static bool beyond_slop(const touch_t *touch, int x, int y)
{
    int dx = x - touch->start_x, dy = y - touch->start_y;
    return dx * dx + dy * dy > touch->tap_slop_px * touch->tap_slop_px;
}

void touch_update(touch_t *touch)
{
    uint64_t now = time_us_64();
    if (now - touch->last_poll_us < TOUCH_POLL_INTERVAL_US) return;
    touch->last_poll_us = now;

    // Nothing touching and nothing to read - stay off the bus.
    if (!touch->down && touch->backend.data_ready && !touch->backend.data_ready(touch->backend.ctx)) return;

    int rx, ry;
    bool down = touch->backend.read(touch->backend.ctx, &rx, &ry);
    if (!down) {
        if (!touch->down) return;
        touch->down = false;
        touch->release_pending = true;
        touch->release_x = touch->x;
        touch->release_y = touch->y;
        touch->release_was_tap = !touch->dragging && !touch->tap_cancelled;
        touch->last_activity_us = now;
        return;
    }

    int x, y;
    to_screen(touch, rx, ry, &x, &y);
    if (!touch->down) {
        touch->down = true;
        touch->dragging = false;
        touch->tap_cancelled = false;
        touch->start_x = touch->x = x;
        touch->start_y = touch->y = y;
        touch->press_pending = true;
        touch->press_x = x;
        touch->press_y = y;
        touch->drag_dx = touch->drag_dy = 0;
        touch->last_activity_us = now;
        return;
    }

    if (x == touch->x && y == touch->y) return;
    touch->x = x;
    touch->y = y;
    touch->last_activity_us = now;
    if (!touch->dragging && beyond_slop(touch, x, y)) {
        touch->dragging = true;
        touch->drag_from_x = touch->start_x; // the drag includes the slop it took to start
        touch->drag_from_y = touch->start_y;
    }
    if (touch->dragging) {
        touch->drag_dx = x - touch->drag_from_x;
        touch->drag_dy = y - touch->drag_from_y;
    }
}

int touch_get_width(const touch_t *touch) { return touch->width; }
int touch_get_height(const touch_t *touch) { return touch->height; }

bool touch_is_down(const touch_t *touch, int *x, int *y)
{
    if (x) *x = touch->x;
    if (y) *y = touch->y;
    return touch->down;
}

bool touch_take_press(touch_t *touch, int *x, int *y)
{
    if (!touch->press_pending) return false;
    touch->press_pending = false;
    if (x) *x = touch->press_x;
    if (y) *y = touch->press_y;
    return true;
}

bool touch_take_release(touch_t *touch, int *x, int *y, bool *was_tap)
{
    if (!touch->release_pending) return false;
    if (touch->press_pending) return false; // its press first - a caller always sees them in order
    touch->release_pending = false;
    if (x) *x = touch->release_x;
    if (y) *y = touch->release_y;
    if (was_tap) *was_tap = touch->release_was_tap;
    return true;
}

bool touch_is_dragging(const touch_t *touch) { return touch->down && touch->dragging; }

bool touch_take_drag(touch_t *touch, int *dx, int *dy)
{
    int mx = touch->drag_dx, my = touch->drag_dy;
    touch->drag_from_x += mx;
    touch->drag_from_y += my;
    touch->drag_dx = touch->drag_dy = 0;
    if (dx) *dx = mx;
    if (dy) *dy = my;
    return mx != 0 || my != 0;
}

void touch_clear(touch_t *touch)
{
    touch->press_pending = false;
    touch->release_pending = false;
    touch->drag_dx = touch->drag_dy = 0;
    touch->drag_from_x = touch->x;
    touch->drag_from_y = touch->y;
    if (touch->down) touch->tap_cancelled = true;
}

void touch_inject_tap(touch_t *touch, int x, int y)
{
    if (touch->down) return;
    touch->x = touch->press_x = touch->release_x = x;
    touch->y = touch->press_y = touch->release_y = y;
    touch->press_pending = true;
    touch->release_pending = true;
    touch->release_was_tap = true;
    touch->last_activity_us = time_us_64();
}

uint64_t touch_get_last_activity_us(const touch_t *touch) { return touch->last_activity_us; }
