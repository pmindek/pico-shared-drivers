#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

// ST7789 SPI TFT driver: a 16-bit (RGB565) framebuffer, pushed to the panel
// over hardware SPI. Sized for the 240x280 1.69" module in gyros' PINOUT.md
// Plan B, but nothing here is specific to that panel beyond the two
// ST7789_WIDTH/HEIGHT constants - other ST7789 sizes would just need those
// changed (and possibly the offset/BGR/inversion knobs in st7789.c - see
// their own comments).
//
// Unlike this library's i2c drivers (mpr121.h, ads1115.h, aw9523.h), there
// is no presence detection here. i2c has an address ACK; SPI doesn't - the
// master just shifts bits out on a clock it fully controls, whether or not
// anything is listening, so there's no hang risk the way i2c_write_
// blocking() has either (nothing here needs a _timeout_us equivalent).
// This panel's MISO isn't wired (see PINOUT.md - nothing here ever reads
// from it), so there's no read-ID command to confirm it's alive either.
// The only way to know it's there is to look at it.
//
// The framebuffer is internal and static: 240*280*2 = 134,400 bytes, RGB565,
// one uint16_t per pixel, row-major. Comfortably affordable on this board
// (gyros' whole build uses well under that in bss already) - simpler than
// windowed/partial pushes, and there was no memory pressure forcing
// anything cleverer. Nothing is sent to the panel until st7789_show().

#define ST7789_WIDTH 240
#define ST7789_HEIGHT 280

// RGB565: 5 bits red, 6 green, 5 blue, packed MSB-first. These are defined
// in RGB order regardless of how the physical panel is wired (see
// kBgrPanel in st7789.c) - the driver corrects for BGR panels at the
// command level (MADCTL), so a color named here always means the same
// thing on screen.
#define ST7789_RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))
#define ST7789_BLACK ST7789_RGB565(0, 0, 0)
#define ST7789_WHITE ST7789_RGB565(255, 255, 255)
#define ST7789_RED ST7789_RGB565(255, 0, 0)
#define ST7789_GREEN ST7789_RGB565(0, 255, 0)
#define ST7789_BLUE ST7789_RGB565(0, 0, 255)
#define ST7789_YELLOW ST7789_RGB565(255, 255, 0)
#define ST7789_CYAN ST7789_RGB565(0, 255, 255)
#define ST7789_MAGENTA ST7789_RGB565(255, 0, 255)

// Claims spi (calls spi_init() itself - do not init it yourself first) and
// the 4 GPIOs, drives a hardware reset pulse, and sends the panel's init
// sequence. sck_pin/mosi_pin must be a valid SPI0/SPI1 alternate-function
// pair for whichever `spi` instance you pass (see PINOUT.md's own note on
// how those were chosen for gyros) - cs_pin/dc_pin/rst_pin are plain
// GPIOs, any free pin works. bl_pin is claimed as PWM (GPIO_FUNC_PWM), not
// a plain output - see st7789_set_backlight_level().
//
// Returns void, not bool - see this header's own comment on why presence
// can't be detected. Call it once, before anything else here.
void st7789_init(spi_inst_t *spi, uint sck_pin, uint mosi_pin, uint cs_pin, uint dc_pin, uint rst_pin, uint bl_pin);

// 0 (off) to 100 (full brightness), via PWM - values above 100 clamp to 100.
// Independent of st7789_show()/the framebuffer, same convention as
// ssd1306_set_display_on(): whatever's already on screen stays there, just
// dimmer or brighter.
void st7789_set_backlight_level(uint8_t percent);

// Convenience wrapper: st7789_set_backlight_level(on ? 100 : 0).
void st7789_set_backlight(bool on);

// Framebuffer drawing - nothing reaches the panel until st7789_show().
// Coordinates outside 0..ST7789_WIDTH-1 / 0..ST7789_HEIGHT-1 are clipped
// (silently ignored/truncated), same convention as ssd1306's own drawing
// calls.
void st7789_clear(uint16_t color);
void st7789_draw_pixel(int x, int y, uint16_t color);
void st7789_fill_rect(int x, int y, int w, int h, uint16_t color);
void st7789_draw_circle(int cx, int cy, int r, uint16_t color);
void st7789_fill_circle(int cx, int cy, int r, uint16_t color);

// Reads back whatever's currently in the framebuffer at (x,y) - i.e.
// exactly what the next st7789_show()/st7789_show_rect() covering this
// pixel would push to the panel, not necessarily what's physically lit
// right now if that hasn't happened yet. Out-of-bounds coordinates
// return black rather than reading outside s_buf. For callers that need
// to blend a new draw against whatever's *actually* already rendered
// there (translucent overlays, etc.) instead of re-deriving an
// approximation of it by hand.
uint16_t st7789_get_pixel(int x, int y);

// Pushes the whole framebuffer to the panel over SPI - one CASET/RASET
// covering the full screen, then one RAMWR burst of all 134,400 bytes.
// Blocking; see st7789.c's own comment on the clock speed / how long this
// takes.
void st7789_show(void);

// Same as st7789_show(), but only pushes the given sub-rectangle - a
// smaller CASET/RASET window, then only that rectangle's own pixels out of
// the framebuffer. For anything that redraws a small moving element over
// an otherwise-static background (a game's sprites, say): pushing just the
// old-and-new bounding box of what actually changed is dramatically
// cheaper than a full st7789_show() every frame, since the SPI transfer
// itself - not the framebuffer drawing - is what a full push actually
// costs the most time on (see st7789.c's own timing comment on
// ST7789_SPI_HZ). Coordinates outside the panel are clipped, same
// convention as every st7789_* drawing call; a fully-clipped-away
// rectangle is a no-op.
void st7789_show_rect(int x, int y, int w, int h);

// ---- Text ----
//
// A font: fixed-width glyphs for a contiguous range of character codes,
// stored column-major - identical layout to (and, deliberately, the exact
// same on-disk glyph data as) ssd1306_font_t in ../ssd1306/ssd1306.h. Text
// is a hand-drawn asset, not a display-specific one, so the bitmaps
// themselves are shared rather than redrawn or duplicated for this driver;
// only the *type* is separate, so that using text here never requires
// linking the unrelated ssd1306/i2c code. See st7789.c for exactly which
// raw glyph-data headers get pulled in.
//
// bit N of glyphs[(code - first_code) * glyph_width * bytes_per_col + col *
// bytes_per_col + N/8], bit N%8 of that byte = row N of that column.
// bytes_per_col = (glyph_height + 7) / 8.
typedef struct {
    uint8_t glyph_width;
    uint8_t glyph_height;
    const uint8_t *glyphs;
    uint8_t first_code;
    uint16_t glyph_count;
} st7789_font_t;

// Every font ssd1306 has that's meant for actual text (not
// ssd1306_font_main_menu/main_menu_32, which are icon slots, or
// ssd1306_font_cp437, 256 glyphs of a code page nothing here needs yet) -
// same names, same glyph data, same metrics as their ssd1306_font_* twins.
extern const st7789_font_t st7789_font_adafruit;
extern const st7789_font_t st7789_font_chonk;
extern const st7789_font_t st7789_font_mini;
extern const st7789_font_t st7789_font_narrow;
extern const st7789_font_t st7789_font_micro;
extern const st7789_font_t st7789_font_big;
extern const st7789_font_t st7789_font_tall;

// Draws `text` starting at (x,y), one glyph_width+spacing_px pixels per
// character, left to right. Paints every pixel of every cell - the "on"
// bits in fg, the "off" bits (and the spacing gap between characters) in
// bg - not just the lit pixels, same convention ssd1306_draw_text() uses
// and for the same reason: redrawing a different (e.g. blank, for erasing)
// character over an existing one must not leave any of the old glyph
// behind. A character code outside the font's range paints as a blank
// (all-bg) cell rather than being skipped, so the cursor still advances by
// one cell width - a caller stepping through a string never gets thrown
// off by one unsupported character.
//
// There's no transparent mode (skip bg entirely, leave whatever was
// already there) - not needed yet. A caller wanting text over something
// already drawn can bg-fill first with st7789_fill_rect() using whatever
// is already there, or this can grow a transparent variant later if
// something actually needs one.
void st7789_draw_text(int x, int y, const char *text, const st7789_font_t *font, int spacing_px, uint16_t fg,
                      uint16_t bg);

#ifdef __cplusplus
}
#endif
