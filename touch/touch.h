#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Touch screen input, independent of any display or touch chip - the same
// split as Pigs: a backend (touch_ft6336.h, ...) reads the chip, this
// layer turns its readings into screen coordinates and gestures.
//
// Screen coordinates: the touch controller's own coordinates turned by a
// fixed orientation (swap X/Y, then mirror) so that (0,0) is the screen's
// top-left as the display draws it - the touch counterpart of a display
// driver's own swap/mirror settings, set once at init. Nothing here knows
// about Pigs' runtime rotation; a caller drawing rotated maps the point
// itself.
//
// Gestures, from one finger (a second one is ignored):
//   press    - the finger went down (where)
//   release  - it came up (where), and whether that was a *tap*: it never
//              moved more than tap_slop_px from where it went down
//   drag     - movement since the last touch_take_drag(), once the finger
//              has moved past tap_slop_px (so a tap never also drags)
// Each is kept until taken, like button_pressed(): a caller polls with
// touch_take_*() once per update, and a press and its release that both
// happened since the last poll still arrive in order.
//
// Polled: call touch_update() once per main-loop pass. It reads the chip
// at most every TOUCH_POLL_INTERVAL_US, and not at all while the backend's
// data_ready() says nothing is happening.

#define TOUCH_POLL_INTERVAL_US 10000 // 100 reads a second while touched - untuned first guess

typedef struct {
    // The first touch point in the controller's own coordinates; true if a
    // finger is down. Required.
    bool (*read)(void *ctx, int *x, int *y);
    // True when a read could find a finger (e.g. the chip's INT line is
    // asserted). touch_update() skips the bus while it's false and no finger
    // is down. May be null - then it always reads.
    bool (*data_ready)(void *ctx);
    void *ctx;
} touch_backend_t;

typedef struct {
    int native_width, native_height; // the controller's coordinate range: x in [0, native_width)
    bool swap_xy;                    // applied first - the screen is then native_height wide
    bool mirror_x, mirror_y;         // then mirrored within the screen
} touch_orientation_t;

typedef struct {
    touch_backend_t backend;
    touch_orientation_t orientation;
    int width, height; // screen size, after the swap
    int tap_slop_px;

    uint64_t last_poll_us;
    bool down;
    int x, y;             // current (or last) finger position
    int start_x, start_y; // where it went down
    bool dragging;        // moved past tap_slop_px since going down
    bool tap_cancelled;   // touch_clear() during this touch - its release isn't a tap

    // Kept until taken.
    bool press_pending;
    int press_x, press_y;
    bool release_pending;
    int release_x, release_y;
    bool release_was_tap;
    int drag_dx, drag_dy;
    int drag_from_x, drag_from_y; // drag_dx/dy are measured from here

    uint64_t last_activity_us; // last press, release or movement; 0 = never
} touch_t;

// `touch` must stay valid while in use. tap_slop_px: how far a finger may
// wander and still count as a tap.
void touch_init(touch_t *touch, const touch_backend_t *backend, const touch_orientation_t *orientation,
                int tap_slop_px);

// Polls the backend (rate-limited, see above) and updates the gestures.
void touch_update(touch_t *touch);

// Screen size (after the orientation's swap).
int touch_get_width(const touch_t *touch);
int touch_get_height(const touch_t *touch);

// Whether a finger is down right now, and where. Consumes nothing.
bool touch_is_down(const touch_t *touch, int *x, int *y);

// True once per finger-down, with where it went down.
bool touch_take_press(touch_t *touch, int *x, int *y);

// True once per finger-up, with where it came up and whether it was a tap
// (never moved past tap_slop_px). was_tap may be null.
bool touch_take_release(touch_t *touch, int *x, int *y, bool *was_tap);

// True while the current touch has moved past tap_slop_px (so it won't be
// a tap). Consumes nothing.
bool touch_is_dragging(const touch_t *touch);

// Movement since the last call, while dragging (0,0 otherwise). Returns
// true if anything moved.
bool touch_take_drag(touch_t *touch, int *dx, int *dy);

// Drops every pending press/release/drag - for a screen switch, or a touch
// that only woke the screen. A finger still down keeps being tracked, but
// its eventual release no longer counts as a tap.
void touch_clear(touch_t *touch);

// Test aid: queues a press and its release at (x, y), screen coordinates,
// as if a finger tapped there - for driving a screen from a PC (e.g. over
// serial) without touching it. Ignored while a real finger is down.
void touch_inject_tap(touch_t *touch, int x, int y);

// time_us_64() of the last press, release or movement - 0 if never.
// Consumes nothing (for idle timeouts).
uint64_t touch_get_last_activity_us(const touch_t *touch);

#ifdef __cplusplus
}
#endif
