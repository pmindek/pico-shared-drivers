#pragma once

#include <stdbool.h>
#include "ssd1306.h"

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
//
// Everything in this API is in character-grid terms (column/row indices,
// column 0 = leftmost, row 0 = top line) - never raw pixels, so it composes
// without the caller needing to know SSD1306_CHAR_WIDTH/HEIGHT at all.

// Character grid size - SSD1306_WIDTH/HEIGHT divided by
// SSD1306_CHAR_WIDTH/HEIGHT, rounded up so a partial trailing cell still
// counts as a column/row.
#define SSD1306_CONSOLE_COLS ((SSD1306_WIDTH + SSD1306_CHAR_WIDTH - 1) / SSD1306_CHAR_WIDTH)
#define SSD1306_CONSOLE_ROWS ((SSD1306_HEIGHT + SSD1306_CHAR_HEIGHT - 1) / SSD1306_CHAR_HEIGHT)

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

// Highlights the character at grid position (col, row).
void ssd1306_console_highlight_char(int col, int row);

// Highlights every character in the (col, row, cols, rows) grid rectangle -
// e.g. ssd1306_console_highlight_rect(0, 0, SSD1306_CONSOLE_COLS, 1) for
// the entire first line.
void ssd1306_console_highlight_rect(int col, int row, int cols, int rows);

// Un-highlights every currently-highlighted character cell.
void ssd1306_console_dehighlight_all(void);

// Classic MS-DOS/CP437 double-line box-drawing character codes (Norton
// Commander style), for use with ssd1306_console_draw_border() below or
// directly with ssd1306_draw_text()/ssd1306_console_write().
#define SSD1306_CONSOLE_BOX_TOP_LEFT     ((char)0xC9)
#define SSD1306_CONSOLE_BOX_TOP_RIGHT    ((char)0xBB)
#define SSD1306_CONSOLE_BOX_BOTTOM_LEFT  ((char)0xC8)
#define SSD1306_CONSOLE_BOX_BOTTOM_RIGHT ((char)0xBC)
#define SSD1306_CONSOLE_BOX_HORIZONTAL   ((char)0xCD)
#define SSD1306_CONSOLE_BOX_VERTICAL     ((char)0xBA)

// Draws a double-line border rectangle (outer size cols x rows, in
// character cells) with its top-left at grid position (col, row). Pure
// overlay: draws directly at those cells via ssd1306_draw_text(), same as
// the highlight functions - does not touch the cursor, does not go through
// write()/new_line(), and so can never trigger a scroll. Only the border
// cells themselves (the outermost ring) are touched; whatever's already in
// the interior is left alone - reserve blank cells there first (e.g. a
// leading space in each content line, an empty line above/below) if you
// don't want the border overdrawing real content.
void ssd1306_console_draw_border(int col, int row, int cols, int rows);

#ifdef __cplusplus
}
#endif
