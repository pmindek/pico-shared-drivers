#pragma once

#include <stdbool.h>

// Simple text console built on ssd1306_draw_text() - tracks a cursor
// position across calls instead of taking explicit coordinates every time.
// Cursor advance is computed from SSD1306_CHAR_WIDTH/HEIGHT and the live
// screen size each time, not a precomputed columns-per-line count, so this
// keeps working if either changes later and isn't an exact multiple of the
// other.
//
// ssd1306_console_write()/write_ln() recognize '\n' (next line), '\r'
// (column 0, same line), and '\b' (back up one character and erase it, if
// not already at the start of the line).

#ifdef __cplusplus
extern "C" {
#endif

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

// Highlighting inverts a character cell's pixels and is tracked per cell -
// highlighting an already-highlighted cell is a no-op, so calling this
// twice on the same character does NOT toggle it back off (which is what
// would happen from blindly inverting the pixels on every call).

// Highlights the character cell containing pixel (x, y).
void ssd1306_console_highlight_char(int x, int y);

// Highlights every character cell touched by the (x, y, w, h) pixel
// rectangle (a cell counts as touched if the rectangle overlaps any part of
// it - whole cells get highlighted, not partial pixels).
void ssd1306_console_highlight_rect(int x, int y, int w, int h);

// Un-highlights every currently-highlighted character cell.
void ssd1306_console_dehighlight_all(void);

#ifdef __cplusplus
}
#endif
