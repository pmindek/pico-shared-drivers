#pragma once

#include <stdint.h>
#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

// A reusable "window" chrome: a title line, a checkered divider below it,
// and a scrollable content area beneath that - what screen_sequence.cpp
// used to hand-draw for itself, factored out so any screen can reuse it.
//
// The content area is always SSD1306_WINDOW_CONTENT_WIDTH px wide (leaving
// room for a 3px scrollbar plus a 2px gap before it at the right edge -
// both only actually drawn when the content doesn't fit). Its *logical*
// height can be taller than fits on screen at once -
// ssd1306_window_draw()'s scroll_y parameter picks which vertical slice is
// currently visible, but deciding what that should be (e.g. following a
// cursor as it moves) is entirely up to the caller; the window only ever
// renders whatever scroll position it's given and draws a matching
// scrollbar - it never looks at input or tracks a cursor of its own.
//
// Content is drawn with ssd1306_window_begin_content()/end_content(),
// which redirect the ssd1306 driver's drawing calls (ssd1306_draw_pixel/
// text/fill_rect/..., and so ssd1306_console_write() too - see
// ssd1306_console.h) into the window's own off-screen canvas instead of
// the real display, with coordinates relative to the canvas's own top-left
// (0, 0) regardless of scroll position - scrolling is purely a
// ssd1306_window_draw()-time concern, not something content drawing needs
// to account for.
//
// The window doesn't allocate anything - `content_buf` is caller-owned
// storage (e.g. a static array sized with
// SSD1306_WINDOW_CONTENT_BYTES(max_height)), so the tallest the content can
// ever be is fixed for the window's lifetime by however big a buffer was
// handed to ssd1306_window_init(); the *current* logical height (how much
// of that capacity is actually in use right now) can be changed at any
// time via ssd1306_window_set_content_height().
#define SSD1306_WINDOW_CONTENT_WIDTH (SSD1306_WIDTH - 5)

// Bytes of backing storage a window's content canvas needs for a given
// (maximum) logical content height, in px - size a static buffer with this
// and pass it to ssd1306_window_init().
#define SSD1306_WINDOW_CONTENT_BYTES(height_px) SSD1306_CANVAS_BYTES(SSD1306_WINDOW_CONTENT_WIDTH, (height_px))

typedef struct {
    char title[24];
    const ssd1306_font_t *title_font; // also determines the divider/content viewport's y position
    int viewport_y;          // where the content viewport starts, in display pixels (set by ssd1306_window_init())
    int viewport_height;     // how many content px are visible on screen at once (set by ssd1306_window_init())
    int content_capacity_px; // the max content_buf passed to ssd1306_window_init() can hold
    ssd1306_canvas_t canvas; // canvas.buf = content_buf; canvas.height = current logical content height
} ssd1306_window_t;

// Sets up `win`: title text, the font it's drawn in (which also determines
// where the divider/content viewport start - see viewport_y/viewport_height
// above), and the content canvas (content_buf, sized with
// SSD1306_WINDOW_CONTENT_BYTES(content_buf_capacity_px) - see above). The
// logical content height starts out equal to the full capacity; call
// ssd1306_window_set_content_height() afterwards if the actual content is
// shorter than that.
void ssd1306_window_init(ssd1306_window_t *win, const char *title, const ssd1306_font_t *title_font,
                          uint8_t *content_buf, int content_buf_capacity_px);

// Updates the title text (takes effect on the next ssd1306_window_draw()).
void ssd1306_window_set_title(ssd1306_window_t *win, const char *title);

// Changes the content area's current logical height - e.g. when the
// underlying data (a note sequence, a list, ...) grows or shrinks. Clamped
// to the capacity passed to ssd1306_window_init(). Existing canvas content
// is left as-is - this doesn't clear or preserve anything meaningful past
// the new height, it just changes what ssd1306_window_draw() is willing to
// scroll through; redraw the content (ssd1306_window_begin_content()/
// end_content()) after calling this if the underlying data actually
// changed too.
void ssd1306_window_set_content_height(ssd1306_window_t *win, int content_height_px);

// Redirects the ssd1306 driver's drawing calls into this window's content
// canvas (ssd1306_set_target(&win->canvas)) and clears it. Draw the
// window's content after this, then call ssd1306_window_end_content()
// before drawing anything else (most importantly, before
// ssd1306_window_draw() - it draws onto the real display, and would end up
// inside the canvas instead if called first).
void ssd1306_window_begin_content(ssd1306_window_t *win);

// Restores drawing calls to the real display (ssd1306_set_target(NULL)).
void ssd1306_window_end_content(void);

// Draws the window onto the real display: title, divider, the slice of the
// content canvas starting at content row `scroll_y` px (clamped so the
// viewport never runs past the canvas), and a scrollbar (drawn only if the
// logical content height exceeds the viewport - see ssd1306_scrollbar.h).
// Does not call ssd1306_show() - same convention as every other draw call
// in this driver; the caller decides when to push, typically once after
// its whole screen (window included) has been redrawn.
void ssd1306_window_draw(const ssd1306_window_t *win, int scroll_y);

#ifdef __cplusplus
}
#endif
