#include "ssd1306_scrollbar.h"
#include "ssd1306.h"

void ssd1306_draw_vertical_scrollbar(int x, int y, int height, int total_items, int visible_items,
                                      int first_visible_item) {
    if (height <= 0) return;

    // Always clear this scrollbar's whole footprint first, so a thumb that
    // has moved - or a scrollbar that isn't needed at all this time -
    // doesn't leave stale pixels behind from a previous call.
    ssd1306_fill_rect(x, y, 3, height, false);

    if (total_items <= visible_items) return; // everything fits - no scrollbar needed

    // Track: thin full-height line, always representing the whole list -
    // checkered rather than solid, so it reads visually as "the whole
    // range" vs. the thumb's solid "your current position within it".
    ssd1306_set_checkerboard_debug(true);
    ssd1306_fill_rect(x + 1, y, 1, height, true);
    ssd1306_set_checkerboard_debug(false);

    // Thumb: proportional segment of the track, at least 1px so it's never
    // invisible even when total_items is very large relative to height.
    int thumb_h = (visible_items * height) / total_items;
    if (thumb_h < 1) thumb_h = 1;

    int thumb_y = y + (first_visible_item * height) / total_items;
    if (thumb_y + thumb_h > y + height) thumb_y = y + height - thumb_h; // keep it fully on-screen at the bottom edge

    ssd1306_fill_rect(x, thumb_y, 3, thumb_h, true);
}

void ssd1306_draw_horizontal_scrollbar(int x, int y, int width, int total_items, int visible_items,
                                        int first_visible_item) {
    if (width <= 0) return;

    ssd1306_fill_rect(x, y, width, 3, false);

    if (total_items <= visible_items) return;

    ssd1306_set_checkerboard_debug(true);
    ssd1306_fill_rect(x, y + 1, width, 1, true);
    ssd1306_set_checkerboard_debug(false);

    int thumb_w = (visible_items * width) / total_items;
    if (thumb_w < 1) thumb_w = 1;

    int thumb_x = x + (first_visible_item * width) / total_items;
    if (thumb_x + thumb_w > x + width) thumb_x = x + width - thumb_w;

    ssd1306_fill_rect(thumb_x, y, thumb_w, 3, true);
}
