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

// "Adafruit" - Adafruit_GFX's own default font ("glcdfont"), 5x8 glyphs
// (visually ~5x7; the 8th row is blank for most characters, used only for
// descenders on a few), covering all 256 codes. Follows CP437's code-page
// layout but isn't a real CP437 bitmap font - see ssd1306_font_cp437 for
// that.
extern const ssd1306_font_t ssd1306_font_5x7;

// "Chonk" - 7x11 glyphs, ASCII 32 (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_7x11;

// "Mini" - 6x7 glyphs, ASCII 32 (space) through 127 (DEL).
extern const ssd1306_font_t ssd1306_font_6x7;

// "Narrow" - 7x11 glyphs (thinner strokes than ssd1306_font_7x11), ASCII 32
// (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_7x11_narrow;

// "Micro" - 4x7 glyphs, ASCII 32 (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_4x7;

// "Big" - 10x12 glyphs, ASCII 32 (space) through 126 ('~').
extern const ssd1306_font_t ssd1306_font_10x12;

// "CP437" - the full IBM PC/VGA codepage 437 at its authentic 9x16
// text-mode size (see generate_cp437.py), covering all 256 codes - not to
// be confused with ssd1306_font_5x7 ("Adafruit"), a different, much
// smaller font that just shares CP437's code-page layout; kept around for
// its own sake, not superseded by this one.
extern const ssd1306_font_t ssd1306_font_cp437;

// An off-screen drawing target: a 1bpp paged framebuffer just like the
// display's own, but any width/height - see ssd1306_set_target(). buf must
// be at least SSD1306_CANVAS_BYTES(width, height) bytes, caller-owned (nothing
// here ever allocates memory); its contents are undefined until something
// draws into it (typically ssd1306_clear() right after ssd1306_set_target()).
typedef struct {
    int width;
    int height;
    uint8_t *buf;
} ssd1306_canvas_t;

// Bytes of backing storage an ssd1306_canvas_t of the given size needs.
#define SSD1306_CANVAS_BYTES(width, height) ((size_t)(width) * (size_t)(((height) + 7) / 8))

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

// Redirects every drawing call above (and so ssd1306_draw_text() and the
// console, both built entirely on top of them) to `canvas` instead of the
// display's own framebuffer, until the next ssd1306_set_target() call. Pass
// NULL to go back to the display - the default target, so most code never
// needs to call this at all. ssd1306_show() always pushes the display's own
// framebuffer specifically, regardless of the current target - an
// off-screen canvas is made visible by ssd1306_blit_canvas()ing it onto the
// display (after ssd1306_set_target(NULL)), not by targeting it directly.
void ssd1306_set_target(ssd1306_canvas_t *canvas);

// The current target's dimensions (SSD1306_WIDTH/HEIGHT when it's the
// display) - what ssd1306_draw_pixel() and friends are currently clipping
// against. For code (like the console) that needs to size itself to
// whatever it's currently drawing into rather than assuming the display.
int ssd1306_target_width(void);
int ssd1306_target_height(void);

// True if the current target is the display (i.e. ssd1306_set_target()
// hasn't redirected drawing to an off-screen canvas). Lets code that
// normally pushes to the display after drawing (e.g. the console) skip
// that push when it's actually drawing off-screen, where it would be both
// meaningless and - since it'd still push the display's unrelated,
// unchanged content - pure wasted i2c traffic.
bool ssd1306_target_is_display(void);

// Copies a `w` x `h` region of `src` starting at (src_x, src_y) onto the
// *current target* at (dst_x, dst_y) - each destination pixel goes through
// ssd1306_draw_pixel(), so normal clipping/checkerboard-debug rules apply.
// Source pixels outside src's own bounds (e.g. src_y < 0, or src_y + h  >
// src->height) read as off, rather than being an error - this is what lets
// a window blit a partial/short slice of its content canvas without the
// caller special-casing "content shorter than the viewport".
void ssd1306_blit_canvas(const ssd1306_canvas_t *src, int src_x, int src_y, int w, int h, int dst_x, int dst_y);

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

// Debug/test aid: when enabled, every pixel write - from any caller,
// through any of this driver's drawing functions (ssd1306_draw_pixel(),
// ssd1306_invert_rect(), and so everything built on top of them: lines,
// circles, rects, text, the console, ...) - only actually takes effect at
// checkerboard positions (where (x ^ y) & 1 == 0); everywhere else the
// write is silently skipped, leaving whatever was already there. The
// result is that anything drawn comes out as a checkerboard pattern. Since
// this is enforced at the driver's core pixel-write choke points rather
// than by each caller, it doubles as a way to confirm some new drawing
// path genuinely goes through them - code that bypasses them (writing to
// the framebuffer directly) would show up solid, not checkerboarded.
// ssd1306_clear() and ssd1306_scroll_up() are unaffected (they move/reset
// existing content rather than draw new pixels). Off by default.
void ssd1306_set_checkerboard_debug(bool enabled);

#ifdef __cplusplus
}
#endif
