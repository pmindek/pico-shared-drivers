#include "pigs_ili9341.h"

#include "ili9341.h"

// pigs_color_t is the same RGB565 packing as ILI9341_RGB565() - these just
// forward, same as pigs_st7789.c.
static void backend_draw_pixel(int x, int y, pigs_color_t color) { ili9341_draw_pixel(x, y, color); }
static void backend_fill_rect(int x, int y, int w, int h, pigs_color_t color) { ili9341_fill_rect(x, y, w, h, color); }
static pigs_color_t backend_read_pixel(int x, int y) { return ili9341_get_pixel(x, y); }

static const pigs_backend_t kBackend = {
    .width = ILI9341_WIDTH,
    .height = ILI9341_HEIGHT,
    .is_color = true,
    .draw_pixel = backend_draw_pixel,
    .fill_rect = backend_fill_rect,
    .show = ili9341_show,
    .show_rect = ili9341_show_rect,
    .read_pixel = backend_read_pixel,
};

const pigs_backend_t *pigs_backend_ili9341(void) { return &kBackend; }
