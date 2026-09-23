#include "pigs_st7789.h"

#include "st7789.h"

// pigs_color_t is bit-for-bit the same RGB565 packing as ST7789_RGB565()
// (see pigs.h's own comment) - these just forward, no conversion needed.
static void backend_draw_pixel(int x, int y, pigs_color_t color) { st7789_draw_pixel(x, y, color); }
static void backend_fill_rect(int x, int y, int w, int h, pigs_color_t color) { st7789_fill_rect(x, y, w, h, color); }

static const pigs_backend_t kBackend = {
    .width = ST7789_WIDTH,
    .height = ST7789_HEIGHT,
    .is_color = true,
    .draw_pixel = backend_draw_pixel,
    .fill_rect = backend_fill_rect,
    .show = st7789_show,
};

const pigs_backend_t *pigs_backend_st7789(void) { return &kBackend; }
