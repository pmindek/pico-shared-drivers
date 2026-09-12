#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

// ssd1306_draw_text()'s per-character cell size, i.e. ssd1306_font_5x7's
// cell_width/cell_height (see ssd1306_font_t below) - the console/highlight/
// scrollbar helpers are all built around this fixed grid, so it stays a
// pair of top-level macros rather than requiring an ssd1306_font_t lookup
// just to lay out a line of text. A font used only via ssd1306_draw_text_ex()
// directly, without going through those helpers, can have any cell size of
// its own - it isn't tied to these values.
#define SSD1306_CHAR_WIDTH  6
#define SSD1306_CHAR_HEIGHT 9

#ifdef __cplusplus
extern "C" {
#endif

// A font: 256 fixed-width glyphs (one per byte value, CP437-style), stored
// column-major - `glyphs[code * glyph_width + col]` is one column of the
// glyph for character `code`, bit N of that byte = row N. glyph_height must
// be <= 8 so a column always fits in one byte.
//
// cell_width/cell_height are the full per-character advance, i.e. what
// ssd1306_draw_text_ex() actually steps by and repaints - normally a bit
// bigger than glyph_width/glyph_height so there's a blank column/row of
// gap between characters/lines; a font with no built-in gap just sets
// cell_width == glyph_width (and/or cell_height == glyph_height).
typedef struct {
    uint8_t glyph_width;
    uint8_t glyph_height;
    uint8_t cell_width;
    uint8_t cell_height;
    const uint8_t *glyphs; // glyph_width bytes per character code, 256 codes
} ssd1306_font_t;

// The font ssd1306_draw_text() uses - CP437, 5x8 glyphs (visually ~5x7; the
// 8th row is blank for most characters, used only for descenders on a few),
// 6x9 cell (1px gap column + 1px gap row).
extern const ssd1306_font_t ssd1306_font_5x7;

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

// Draws text with its top-left corner at (x, y), using the given font (see
// ssd1306_font_t above). Characters that run off the right/bottom edge are
// clipped, not wrapped. Always repaints the full character cell (on AND off
// pixels, including any gap column/row) so redrawing over existing content
// - e.g. to erase a character - works correctly.
void ssd1306_draw_text_ex(int x, int y, const char *text, const ssd1306_font_t *font);

// ssd1306_draw_text_ex(x, y, text, &ssd1306_font_5x7) - the original,
// single-font entry point. Behavior unchanged; kept as the default so
// existing callers don't need to care that other fonts exist.
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
