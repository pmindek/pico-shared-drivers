#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

// ssd1306_draw_text()'s per-character advance - the font glyph itself is
// 5 columns x 8 rows, plus a 1px gap between characters.
#define SSD1306_CHAR_WIDTH  6
#define SSD1306_CHAR_HEIGHT 9

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the display over the given i2c instance/address.
// Caller must have already called i2c_init() and set GPIO functions/pull-ups.
void ssd1306_init(i2c_inst_t *i2c, uint8_t addr);

// Framebuffer drawing (nothing is sent to the display until ssd1306_show()).
void ssd1306_clear(void);
void ssd1306_draw_pixel(int x, int y, bool on);
void ssd1306_draw_line(int x0, int y0, int x1, int y1, bool on);
void ssd1306_draw_circle(int cx, int cy, int r, bool on);
void ssd1306_fill_circle(int cx, int cy, int r, bool on);
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
void ssd1306_invert_rect(int x, int y, int w, int h);

// Draws text (5x7 CP437 font, 6px per character advance) with its top-left
// corner at (x, y). Characters that run off the right/bottom edge are
// clipped, not wrapped. Always repaints the full character cell (on AND off
// pixels, including the trailing gap column) so redrawing over existing
// content - e.g. to erase a character - works correctly.
void ssd1306_draw_text(int x, int y, const char *text);

// Shifts the whole framebuffer's content up by `rows` pixels; rows exposed
// at the bottom are cleared, rows shifted off the top are lost. `rows` does
// not need to be a multiple of anything.
void ssd1306_scroll_up(int rows);

// Pushes the framebuffer to the display over i2c.
void ssd1306_show(void);

// Turns the physical panel on/off (SSD1306 display on/off command) without
// touching the framebuffer - the last-shown content reappears as-is when
// turned back on, no redraw needed. For e.g. an idle timeout, not a
// replacement for ssd1306_clear().
void ssd1306_set_display_on(bool on);

#ifdef __cplusplus
}
#endif
