#include "pigs.h"

#include <stddef.h>

// Raw glyph-bitmap data only (no struct, no i2c/spi) - the exact same
// headers ssd1306.c and st7789.c each already include for their own font
// wiring. See pigs.h's own comment on why the data is shared but the
// pigs_font_t *type* deliberately isn't.
#include "../ssd1306/adafruit_5x7.h"
#include "../ssd1306/big_10x12.h"
#include "../ssd1306/chonk_7x11.h"
#include "../ssd1306/cp437.h"
#include "../ssd1306/micro_4x7.h"
#include "../ssd1306/mini_6x7.h"
#include "../ssd1306/narrow_7x11.h"
#include "../ssd1306/tall_9x13.h"

const pigs_font_t pigs_font_adafruit = {5, 8, kFontAdafruit5x7, 0, 256};
const pigs_font_t pigs_font_chonk = {7, 11, kFontChonk7x11, 32, 95};
const pigs_font_t pigs_font_mini = {6, 7, kFontMini6x7, 32, 96};
const pigs_font_t pigs_font_narrow = {7, 11, kFontNarrow7x11, 32, 95};
const pigs_font_t pigs_font_micro = {4, 7, kFontMicro4x7, 32, 95};
const pigs_font_t pigs_font_big = {10, 12, kFontBig10x12, 32, 95};
const pigs_font_t pigs_font_cp437 = {9, 16, kFontCp437, 0, 256};
const pigs_font_t pigs_font_tall = {9, 13, kFontTall9x13, 32, 95};

static const pigs_backend_t *s_backend;
static pigs_rotation_t s_rotation = PIGS_ROTATION_0;

void pigs_init(const pigs_backend_t *backend)
{
    s_backend = backend;
}

void pigs_set_rotation(pigs_rotation_t rotation) { s_rotation = rotation; }
pigs_rotation_t pigs_get_rotation(void) { return s_rotation; }
bool pigs_backend_is_color(void) { return s_backend->is_color; }

static bool rotation_swaps_axes(void)
{
    return s_rotation == PIGS_ROTATION_90 || s_rotation == PIGS_ROTATION_270;
}

int pigs_get_width(void) { return rotation_swaps_axes() ? s_backend->height : s_backend->width; }
int pigs_get_height(void) { return rotation_swaps_axes() ? s_backend->width : s_backend->height; }

// Maps a logical (post-rotation) coordinate to the backend's own native
// (rotation-0) physical coordinate space - the standard 4-case rotation
// transform (same convention Adafruit_GFX's setRotation() uses: recalled
// from having read that implementation before, not re-derived fresh here -
// worth a real visual sanity check on hardware, which birch's own
// auto-rotation demo provides directly). Every draw call below goes
// through this once, so a caller never has to think about rotation at all.
static void to_physical(int x, int y, int *px, int *py)
{
    int w = s_backend->width, h = s_backend->height;
    switch (s_rotation) {
        case PIGS_ROTATION_90:
            *px = w - 1 - y;
            *py = x;
            break;
        case PIGS_ROTATION_180:
            *px = w - 1 - x;
            *py = h - 1 - y;
            break;
        case PIGS_ROTATION_270:
            *px = y;
            *py = h - 1 - x;
            break;
        case PIGS_ROTATION_0:
        default:
            *px = x;
            *py = y;
            break;
    }
}

void pigs_draw_pixel(int x, int y, pigs_color_t color)
{
    int px, py;
    to_physical(x, y, &px, &py);
    s_backend->draw_pixel(px, py, color);
}

void pigs_fill_rect(int x, int y, int w, int h, pigs_color_t color)
{
    if (w <= 0 || h <= 0) return;
    // Two opposite corners of the logical rect, transformed independently -
    // still axis-aligned afterward since rotation is always a multiple of
    // 90 degrees, so this stays a single fast native fill_rect call even
    // under rotation, not a per-pixel loop.
    int px0, py0, px1, py1;
    to_physical(x, y, &px0, &py0);
    to_physical(x + w - 1, y + h - 1, &px1, &py1);
    int rx = px0 < px1 ? px0 : px1;
    int ry = py0 < py1 ? py0 : py1;
    int rw = (px0 < px1 ? px1 - px0 : px0 - px1) + 1;
    int rh = (py0 < py1 ? py1 - py0 : py0 - py1) + 1;
    s_backend->fill_rect(rx, ry, rw, rh, color);
}

void pigs_clear(pigs_color_t color)
{
    // Native full-panel fill, not a logical/rotated one - rotation only
    // changes how logical coordinates map onto the panel, never which
    // physical pixels exist, so there's nothing for a clear to rotate.
    s_backend->fill_rect(0, 0, s_backend->width, s_backend->height, color);
}

// Identical column-major decode to ssd1306_draw_text()/st7789_draw_text()
// (see either's own comment for the full reasoning) - ported, not
// reinvented: this bit-unpacking is already proven against every font
// here, so matching it exactly means no new place for an off-by-one to
// creep in.
void pigs_draw_text(int x, int y, const char *text, const pigs_font_t *font, int spacing_px, pigs_color_t fg,
                     pigs_color_t bg)
{
    int bytes_per_col = (font->glyph_height + 7) / 8;
    int advance = font->glyph_width + spacing_px;

    for (int i = 0; text[i] != '\0'; i++) {
        uint8_t code = (uint8_t)text[i];
        bool has_glyph = code >= font->first_code && code - font->first_code < font->glyph_count;
        const uint8_t *glyph =
            has_glyph ? &font->glyphs[(code - font->first_code) * font->glyph_width * bytes_per_col] : NULL;

        for (int col = 0; col < advance; col++) {
            const uint8_t *col_bytes = (glyph && col < font->glyph_width) ? &glyph[col * bytes_per_col] : NULL;
            for (int row = 0; row < font->glyph_height; row++) {
                bool on = col_bytes && (col_bytes[row / 8] & (1 << (row % 8))) != 0;
                pigs_draw_pixel(x + i * advance + col, y + row, on ? fg : bg);
            }
        }
    }
}

void pigs_show(void)
{
    s_backend->show();
}

void pigs_show_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;

    if (!s_backend->show_rect) {
        // No cheaper partial update on this backend - a full show() is
        // always correct, just not always fast (see pigs_backend_t's own
        // comment).
        s_backend->show();
        return;
    }

    // Same two-opposite-corners transform as pigs_fill_rect() - still
    // axis-aligned afterward since rotation is always a multiple of 90
    // degrees, so this stays one native show_rect() call, not a per-pixel
    // or per-row loop.
    int px0, py0, px1, py1;
    to_physical(x, y, &px0, &py0);
    to_physical(x + w - 1, y + h - 1, &px1, &py1);
    int rx = px0 < px1 ? px0 : px1;
    int ry = py0 < py1 ? py0 : py1;
    int rw = (px0 < px1 ? px1 - px0 : px0 - px1) + 1;
    int rh = (py0 < py1 ? py1 - py0 : py0 - py1) + 1;
    s_backend->show_rect(rx, ry, rw, rh);
}
