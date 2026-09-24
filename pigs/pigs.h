#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pigs: a display-agnostic drawing/UI layer shared across every project's
// screens - one implementation of color, rotation, and text, sitting on top
// of a small per-display "backend" (pigs_st7789.h / pigs_ssd1306.h) that
// only has to supply the display's own native pixel/rect writes and a way
// to push the framebuffer. Every backend gets full RGB on color panels,
// sensible black/white/dithered-gray degradation on monochrome ones, all 4
// rotations, and text - none of it duplicated per display the way
// st7789.c/ssd1306.c each otherwise carry their own separate copy of
// similar drawing code.
//
// A screen written once against Pigs works on whichever display a project
// actually has wired up, in whatever physical orientation that display
// ended up mounted in - a screen never needs to know or care which.

// RGB565, same bit packing as ST7789_RGB565() in st7789.h - deliberately,
// so a caller already holding an ST7789_RGB565(r,g,b) value can pass it
// straight into any pigs_color_t parameter with zero conversion, and the
// ST7789 backend can forward it to the hardware completely unchanged. On a
// monochrome (OLED-class) backend, this same value gets quantized down to
// black, white, or a 50% checkerboard dither by perceived brightness - see
// pigs_ssd1306.c - so a screen can be written once against real colors and
// still degrade sensibly on a 1-bit panel.
typedef uint16_t pigs_color_t;
#define PIGS_RGB(r, g, b) ((pigs_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define PIGS_BLACK PIGS_RGB(0, 0, 0)
#define PIGS_WHITE PIGS_RGB(255, 255, 255)
#define PIGS_GRAY PIGS_RGB(128, 128, 128)
#define PIGS_RED PIGS_RGB(255, 0, 0)
#define PIGS_GREEN PIGS_RGB(0, 255, 0)
#define PIGS_BLUE PIGS_RGB(0, 0, 255)
#define PIGS_YELLOW PIGS_RGB(255, 255, 0)
#define PIGS_CYAN PIGS_RGB(0, 255, 255)
#define PIGS_MAGENTA PIGS_RGB(255, 0, 255)

// How the logical drawing coordinate space sits on the physical panel -
// separate from anything the panel's own driver knows about, since the
// same physical display can end up mounted in different orientations in
// different projects (or even different physical builds of the same
// project). Runtime-settable, not just a fixed mount-time constant - a
// project with an IMU can call pigs_set_rotation() every frame from live
// tilt data for auto-rotation (see birch's screen_pigs_demo.cpp for a real
// example) without needing to know anything about how the panel itself
// works.
typedef enum {
    PIGS_ROTATION_0,
    PIGS_ROTATION_90,
    PIGS_ROTATION_180,
    PIGS_ROTATION_270,
} pigs_rotation_t;

// What a backend has to supply - the display's own primitive writes, in
// its own *native* (unrotated, rotation-0) physical coordinate space.
// Every Pigs draw call does the rotation math itself and hands the
// backend already-transformed physical coordinates - a backend never
// needs to know rotation exists, and never needs to implement more than
// these three operations. See pigs_st7789.c/pigs_ssd1306.c for the two
// existing ones; a new display type is exactly this struct plus a getter,
// nothing else.
typedef struct {
    int width, height; // native panel resolution, rotation 0
    bool is_color;      // true: draw_pixel gets the real RGB565 value.
                         // false (OLED-class): already quantized to
                         // black/white/dithered-gray by the backend itself
                         // before it ever reaches the panel driver -
                         // informational for callers, not load-bearing for
                         // Pigs' own correctness.
    void (*draw_pixel)(int x, int y, pigs_color_t color);
    void (*fill_rect)(int x, int y, int w, int h, pigs_color_t color);
    void (*show)(void);
} pigs_backend_t;

// A font: fixed-width glyphs for a contiguous range of character codes,
// stored column-major - identical layout to (and, deliberately, the exact
// same on-disk glyph data as) ssd1306_font_t/st7789_font_t. A separate
// type so using Pigs never requires linking either of those - see pigs.c
// for exactly which raw glyph-data headers back these.
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
} pigs_font_t;

extern const pigs_font_t pigs_font_adafruit;
extern const pigs_font_t pigs_font_chonk;
extern const pigs_font_t pigs_font_mini;
extern const pigs_font_t pigs_font_narrow;
extern const pigs_font_t pigs_font_micro;
extern const pigs_font_t pigs_font_big;
extern const pigs_font_t pigs_font_tall;

// The only Pigs font covering more than ASCII 32-126 - full IBM PC/VGA
// codepage 437, all 256 codes, 9x16 (see ssd1306_font_cp437's own comment
// for provenance). The one to reach for whenever a string needs a
// character outside plain ASCII (e.g. "ä" at CP437 code 0x84) - every
// other font here simply has no glyph for it.
extern const pigs_font_t pigs_font_cp437;

// Claims this backend - call once, after the backend's own panel init
// (e.g. st7789_init()/ssd1306_init()) has already brought the physical
// display up. Pigs never touches hardware directly, only through the
// backend's three function pointers, so it has no hardware init of its
// own beyond remembering this pointer. `backend` must stay valid for the
// program's lifetime (every existing one is a static const struct, so
// this is never an issue).
void pigs_init(const pigs_backend_t *backend);

void pigs_set_rotation(pigs_rotation_t rotation);
pigs_rotation_t pigs_get_rotation(void);

// Logical (post-rotation) canvas size - swapped from the backend's own
// native width/height at 90/270. What a screen should lay out against,
// never the backend's width/height fields directly, which stay fixed at
// their native (rotation-0) values regardless of the current rotation.
int pigs_get_width(void);
int pigs_get_height(void);

// True if the current backend renders real color (an ST7789-class panel)
// rather than quantizing to black/white/dithered-gray (an SSD1306-class
// one) - for a screen that wants to adapt its own design (e.g. skip a
// color-only accent flourish that would be indistinguishable from a plain
// fill on a monochrome panel), not needed for correctness.
bool pigs_backend_is_color(void);

// Nothing reaches the panel until pigs_show(). Coordinates outside the
// logical canvas are clipped by whichever backend is active, same
// convention as ssd1306/st7789's own drawing calls.
void pigs_clear(pigs_color_t color);
void pigs_draw_pixel(int x, int y, pigs_color_t color);
void pigs_fill_rect(int x, int y, int w, int h, pigs_color_t color);

// Paints every pixel of every cell - the "on" bits in fg, the "off" bits
// (and the inter-character spacing gap) in bg, not just the lit pixels -
// same convention and same reasoning as ssd1306_draw_text()/
// st7789_draw_text(): redrawing a different (e.g. blank) character over an
// existing one must never leave any of the old glyph behind. A character
// code outside the font's range paints as a blank (all-bg) cell rather
// than being skipped, so the cursor still advances by one full cell width.
void pigs_draw_text(int x, int y, const char *text, const pigs_font_t *font, int spacing_px, pigs_color_t fg,
                     pigs_color_t bg);

// Pushes whatever was drawn to the physical panel - a thin wrapper over
// the backend's own show().
void pigs_show(void);

#ifdef __cplusplus
}
#endif
