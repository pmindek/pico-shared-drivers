#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

#ifdef __cplusplus
extern "C" {
#endif

// A font: fixed-width glyphs for a contiguous range of character codes
// starting at first_code (e.g. 32 = space), stored column-major -
// glyphs[(code - first_code) * glyph_width * bytes_per_col(font) + col *
// bytes_per_col(font) + byte] is one byte of one column of one glyph; bit N
// of that column (byte N/8, bit N%8) = row N. bytes_per_col(font) is
// (glyph_height + 7) / 8 - glyph_height isn't limited to 8 like a single
// font byte would cap it to; a tall font just uses more bytes per column.
// Characters outside [first_code, first_code + glyph_count) draw as blank.
//
// A font only describes its glyphs, not any spacing around them - see
// ssd1306_draw_text()'s spacing_px for inter-character gap, and
// ssd1306_console_set_char_spacing()/set_line_spacing() for the console's
// equivalent. Any number of fonts of any size can exist; nothing here is
// tied to one particular font's dimensions.
typedef struct {
    uint8_t glyph_width;
    uint8_t glyph_height;
    const uint8_t *glyphs;
    uint8_t first_code;
    uint16_t glyph_count;
} ssd1306_font_t;

// CP437, 5x8 glyphs (visually ~5x7; the 8th row is blank for most
// characters, used only for descenders on a few), covering all 256 codes.
extern const ssd1306_font_t ssd1306_font_5x7;

// "Chonk" - 7x11 glyphs, ASCII 32 (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_7x11;

// "Mini" - 6x7 glyphs, ASCII 32 (space) through 127 (DEL).
extern const ssd1306_font_t ssd1306_font_6x7;

// "Narrow" - 7x11 glyphs (thinner strokes than ssd1306_font_7x11), ASCII 32
// (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_7x11_narrow;

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

// Draws text with its top-left corner at (x, y), using `font`, advancing
// (font->glyph_width + spacing_px) pixels per character - spacing_px is
// extra blank columns of gap between characters (0 for none; a font whose
// own glyph cells already include their spacing, like ssd1306_font_7x11,
// wants 0 here). Characters that run off the right/bottom edge are
// clipped, not wrapped. Always repaints the full advance width and glyph
// height (on AND off pixels, including the spacing gap) so redrawing over
// existing content - e.g. to erase a character - works correctly.
void ssd1306_draw_text(int x, int y, const char *text, const ssd1306_font_t *font, int spacing_px);

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
