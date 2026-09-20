#include "ssd1306_window.h"
#include "ssd1306_scrollbar.h"
#include <string.h>

void ssd1306_window_init(ssd1306_window_t *win, const char *title, const ssd1306_font_t *title_font,
                          uint8_t *content_buf, int content_buf_capacity_px) {
    ssd1306_window_set_title(win, title);
    win->title_font = title_font;

    // Divider sits 1px below the title's own glyph height; the content
    // viewport starts 2px below that - 1px for the divider's own row, plus
    // 1px of blank breathing room before the content begins.
    win->viewport_y = title_font->glyph_height + 3;
    win->viewport_height = SSD1306_HEIGHT - win->viewport_y;
    if (win->viewport_height < 0) win->viewport_height = 0;

    win->bottom_margin_px = 0;
    win->content_capacity_px = content_buf_capacity_px;
    win->canvas.width = SSD1306_WINDOW_CONTENT_WIDTH;
    win->canvas.height = content_buf_capacity_px;
    win->canvas.buf = content_buf;
}

void ssd1306_window_set_bottom_margin(ssd1306_window_t *win, int margin_px) {
    if (margin_px < 0) margin_px = 0;
    win->bottom_margin_px = margin_px;
    win->viewport_height = SSD1306_HEIGHT - win->viewport_y - margin_px;
    if (win->viewport_height < 0) win->viewport_height = 0;
}

void ssd1306_window_set_title(ssd1306_window_t *win, const char *title) {
    size_t n = strlen(title);
    if (n >= sizeof(win->title)) n = sizeof(win->title) - 1;
    memcpy(win->title, title, n);
    win->title[n] = '\0';
}

void ssd1306_window_set_content_height(ssd1306_window_t *win, int content_height_px) {
    if (content_height_px > win->content_capacity_px) content_height_px = win->content_capacity_px;
    if (content_height_px < 0) content_height_px = 0;
    win->canvas.height = content_height_px;
}

void ssd1306_window_begin_content(ssd1306_window_t *win) {
    ssd1306_set_target(&win->canvas);
    ssd1306_clear();
}

void ssd1306_window_end_content(void) {
    ssd1306_set_target(NULL);
}

void ssd1306_window_draw(const ssd1306_window_t *win, int scroll_y) {
    ssd1306_set_target(NULL); // chrome always draws straight onto the display

    // Clear the whole title-bar band first: ssd1306_draw_text() below only
    // paints the columns win->title's own text actually spans, and the
    // checkered divider only ever sets its alternating "on" pixels, never
    // clears the rest of the row - so without this, anything drawn there
    // before this window existed (e.g. gyros.cpp's boot-time debug_text()
    // log, which writes straight to the display before any screen takes
    // over) - or even just a previous, longer title - would stay stuck
    // forever, since nothing else ever repaints those particular pixels.
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, win->viewport_y, false);

    if (win->bottom_margin_px > 0) {
        ssd1306_fill_rect(0, SSD1306_HEIGHT - win->bottom_margin_px, SSD1306_WIDTH, win->bottom_margin_px, false);
    }

    ssd1306_draw_text(0, 0, win->title, win->title_font, 0);

    // Checkered, not solid - purely decorative (same convention as the
    // scrollbar's own track - see ssd1306_scrollbar.c).
    int divider_y = win->title_font->glyph_height + 1;
    ssd1306_set_checkerboard_debug(true);
    ssd1306_fill_rect(0, divider_y, SSD1306_WIDTH, 1, true);
    ssd1306_set_checkerboard_debug(false);

    int max_scroll = win->canvas.height > win->viewport_height ? win->canvas.height - win->viewport_height : 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    // Reads past the canvas's own bottom edge (when its logical height is
    // shorter than the viewport) come back as off - see
    // ssd1306_blit_canvas()'s own doc comment - so this always paints the
    // whole viewport correctly with no special-casing needed here.
    ssd1306_blit_canvas(&win->canvas, 0, scroll_y, SSD1306_WINDOW_CONTENT_WIDTH, win->viewport_height, 0,
                         win->viewport_y);

    // The 2px gap between the content area and the scrollbar (both cleared
    // above/below, but never this sliver in between) - without this,
    // anything drawn there before this window existed would stay stuck
    // forever, same as the title bar above.
    int gap_x = SSD1306_WINDOW_CONTENT_WIDTH;
    int gap_w = (SSD1306_WIDTH - 3) - SSD1306_WINDOW_CONTENT_WIDTH;
    if (gap_w > 0) ssd1306_fill_rect(gap_x, win->viewport_y, gap_w, win->viewport_height, false);

    // Always called (not just when the content doesn't fit) - it clears its
    // own footprint either way, which is exactly what's needed if content
    // that used to need a scrollbar no longer does.
    ssd1306_draw_vertical_scrollbar(SSD1306_WIDTH - 3, win->viewport_y, win->viewport_height, win->canvas.height,
                                     win->viewport_height, scroll_y);
}
