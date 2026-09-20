#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// A generic vertical scrollbar overlay, reusable by any screen that shows
// a scrollable list/grid taller than fits on screen at once. Pure drawing
// utility with no state of its own - the caller tracks its own scroll
// position and passes it in every time. Draws onto the *current*
// ssd1306_set_target() (see ssd1306.h), same as any other drawing call.
//
// Drawn as a thin (1px) track spanning the full given height, always
// representing the whole list regardless of how much of it is visible,
// plus a wider (3px) "thumb" overlaid on it - sized and positioned to show
// which portion is currently visible. Always clears its own 3px-wide
// footprint first, so a thumb that has moved (or a scrollbar that's no
// longer needed at all - see below) never leaves stale pixels behind from
// a previous call.
//
// x, y, height: pixel position and height of the scrollbar's footprint
//   (3px wide, at columns x..x+2).
// total_items, visible_items, first_visible_item: the list being
//   represented, in whatever unit the caller finds natural (rows, pixels,
//   list entries, ...) - only their ratios matter, not their unit.
//
// Draws nothing but the cleared footprint if total_items <= visible_items
// (everything already fits - no scrollbar needed).
void ssd1306_draw_vertical_scrollbar(int x, int y, int height, int total_items, int visible_items,
                                      int first_visible_item);

// Horizontal counterpart of ssd1306_draw_vertical_scrollbar(): same
// track/thumb look and the same "always clears its own footprint, draws
// nothing more if everything fits" behavior, rotated - a 3px-tall
// footprint at rows y..y+2, `width` px wide starting at x.
void ssd1306_draw_horizontal_scrollbar(int x, int y, int width, int total_items, int visible_items,
                                        int first_visible_item);

#ifdef __cplusplus
}
#endif
