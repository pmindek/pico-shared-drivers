#include "pigs_ssd1306.h"

#include "ssd1306.h"

// Perceived brightness (ITU-R BT.601 luma weights - green reads brighter
// to the eye than red or blue at the same RGB565 magnitude, so a straight
// component average would misjudge e.g. pure blue as darker than it looks
// and pure green as brighter), bucketed into exactly the three things an
// OLED can express: black, white, or - for the middle band, not just an
// exact match - a checkerboard dither standing in for gray.
typedef enum { LEVEL_BLACK, LEVEL_GRAY, LEVEL_WHITE } luma_level_t;

static luma_level_t quantize(pigs_color_t color)
{
    int r = (color >> 11) & 0x1F, g = (color >> 5) & 0x3F, b = color & 0x1F;
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;
    int luma = (r * 299 + g * 587 + b * 114) / 1000; // 0..255
    if (luma < 85) return LEVEL_BLACK;
    if (luma < 170) return LEVEL_GRAY;
    return LEVEL_WHITE;
}

static void backend_draw_pixel(int x, int y, pigs_color_t color)
{
    luma_level_t level = quantize(color);
    bool on = level == LEVEL_WHITE || (level == LEVEL_GRAY && ((x + y) & 1) == 0);
    ssd1306_draw_pixel(x, y, on);
}

static void backend_fill_rect(int x, int y, int w, int h, pigs_color_t color)
{
    luma_level_t level = quantize(color);
    if (level != LEVEL_GRAY) {
        ssd1306_fill_rect(x, y, w, h, level == LEVEL_WHITE);
        return;
    }
    // No solid fill can express a dither - ssd1306_fill_rect() only ever
    // takes one on/off value for the whole rect, so gray has to fall back
    // to per-pixel writes here, same checkerboard pattern as
    // backend_draw_pixel() above.
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            ssd1306_draw_pixel(x + i, y + j, ((x + i + y + j) & 1) == 0);
        }
    }
}

static void backend_show(void) { ssd1306_show(); } // discards the bool - ssd1306_is_present() is the way to check that, not this return

static const pigs_backend_t kBackend = {
    .width = SSD1306_WIDTH,
    .height = SSD1306_HEIGHT,
    .is_color = false,
    .draw_pixel = backend_draw_pixel,
    .fill_rect = backend_fill_rect,
    .show = backend_show,
};

const pigs_backend_t *pigs_backend_ssd1306(void) { return &kBackend; }
