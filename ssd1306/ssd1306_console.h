#pragma once

#include <stdbool.h>
#include "ssd1306.h"

// Simple text console built on ssd1306_draw_text() - tracks a cursor
// position across calls instead of taking explicit coordinates every time.
//
// The font, the gap between characters, and the gap between lines are all
// runtime-configurable (see the ssd1306_console_set_* calls below) - the
// character-grid size (ssd1306_console_cols()/rows()) and every pixel
// position this module computes are derived from whatever's currently set,
// not a fixed font/cell size. Defaults match this module's original,
// long-standing look: ssd1306_font_adafruit, 1px character spacing, 1px line
// spacing.
//
// ssd1306_console_write()/write_ln() recognize '\n' (next line), '\r'
// (column 0, same line), and '\b' (back up one character and erase it, if
// not already at the start of the line).
//
// Everything else in this API is in character-grid terms (column/row
// indices, column 0 = leftmost, row 0 = top line) - never raw pixels, so it
// composes without the caller needing to know any pixel sizes at all.
//
// The console's grid size and every pixel position it computes are always
// relative to the *current* ssd1306_set_target() (see ssd1306.h) - so it
// works just as well drawing into an off-screen canvas (e.g. a window's
// content area, see ssd1306_window.h) as it does drawing straight to the
// display. Calls that would otherwise push to the display (ssd1306_show())
// skip that automatically while the current target isn't the display -
// pushing wouldn't show the off-screen content anyway, and would just be
// wasted i2c traffic re-sending the display's own unrelated, unchanged
// framebuffer.

#ifdef __cplusplus
extern "C" {
#endif

// Sets the font used for everything drawn after this call (existing
// on-screen content is untouched - this doesn't trigger a redraw). Does
// NOT reset the cursor or clear highlight tracking; call
// ssd1306_console_clear() too if you want a clean slate in the new font.
void ssd1306_console_set_font(const ssd1306_font_t *font);

// Extra blank columns/rows between characters/lines, on top of the current
// font's own glyph_width/glyph_height (see ssd1306_draw_text()'s
// spacing_px) - 0 for none. Same "doesn't trigger a redraw" caveat as
// ssd1306_console_set_font().
void ssd1306_console_set_char_spacing(int spacing_px);
void ssd1306_console_set_line_spacing(int spacing_px);

// The character-grid size for the current font/spacing: screen size divided
// by cell size, rounded up so a partial trailing cell still counts as a
// column/row. Highlight tracking is capped to a fixed maximum grid (see
// ssd1306_console.c) - a font/spacing combination small enough to need more
// cells than that is clamped, not out-of-bounds.
int ssd1306_console_cols(void);
int ssd1306_console_rows(void);

// Pixel size of one console cell for the current font/spacing - for
// positioning your own direct ssd1306_draw_text() calls (or anything else)
// so they land exactly where the console's own grid - and so
// ssd1306_console_highlight_rect() and friends - expects them.
int ssd1306_console_cell_width(void);
int ssd1306_console_cell_height(void);

// Clears the screen, pushes it, and resets the cursor to the top-left.
void ssd1306_console_clear(void);

// Whether ssd1306_console_write()/ssd1306_console_write_ln() wrap to the
// next line when a character would run past the right edge, vs. letting it
// clip off-screen. On by default.
void ssd1306_console_set_auto_newline(bool enabled);

// Writes text at the cursor (wrapping per character, not per word, when
// auto-newline is on), advances the cursor, and pushes the result.
void ssd1306_console_write(const char *text);

// ssd1306_console_write() followed by ssd1306_console_new_line().
void ssd1306_console_write_ln(const char *text);

// Moves the cursor to the start of the next line. If that line wouldn't
// fully fit on screen, scrolls existing content up by one line instead
// (like a normal terminal) and stays on the now-blank bottom line.
void ssd1306_console_new_line(void);

// Shifts every highlight rect - both drawing a new one and undoing an
// existing one - by (dx, dy) pixels from the cell's own pixel bounds - e.g.
// dy = -1 (the default) lands the highlight's bottom edge on the 1px gap
// row below the text instead of on the text's own last row, leaving a
// visible unhighlighted border under highlighted text. This is a single
// live setting, not attached to individual highlighted cells - changing it
// while cells are currently highlighted means a later
// ssd1306_console_dehighlight_all() will un-invert them at the new offset,
// not the one they were highlighted at, leaving stray pixels behind. Call
// ssd1306_console_dehighlight_all() first if anything's currently
// highlighted before changing this.
void ssd1306_console_set_highlight_offset(int dx, int dy);

// Highlighting inverts a character cell's pixels and is tracked per cell -
// highlighting an already-highlighted cell is a no-op, so calling this
// twice on the same character does NOT toggle it back off (which is what
// would happen from blindly inverting the pixels on every call).

// Highlights the character at grid position (col, row).
void ssd1306_console_highlight_char(int col, int row);

// Highlights every character in the (col, row, cols, rows) grid rectangle -
// e.g. ssd1306_console_highlight_rect(0, 0, ssd1306_console_cols(), 1) for
// the entire first line.
void ssd1306_console_highlight_rect(int col, int row, int cols, int rows);

// Un-highlights every currently-highlighted character cell.
void ssd1306_console_dehighlight_all(void);

#ifdef __cplusplus
}
#endif
