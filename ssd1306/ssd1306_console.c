#include "ssd1306_console.h"
#include "ssd1306.h"

static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_auto_newline = true;

// Highlighting is shifted 1px up from the cell's actual pixel bounds, so its
// bottom edge lands on the (always-blank) gap row instead of the cell's own
// last text row - a deliberate 1px unhighlighted border under highlighted
// text, for readability. The cost: the top text row of the cell above gets
// swallowed into the highlight instead. On row 0, the shifted-up top edge is
// simply clipped by ssd1306_invert_rect()'s bounds check, losing that 1px
// border on the very first line - accepted as harmless.
#define SSD1306_CONSOLE_HIGHLIGHT_Y_OFFSET (-1)

// Highlight tracking, one flag per character cell.
static bool s_highlighted[SSD1306_CONSOLE_ROWS][SSD1306_CONSOLE_COLS];

static void clear_highlight_tracking(void)
{
    for (int row = 0; row < SSD1306_CONSOLE_ROWS; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_COLS; col++) {
            s_highlighted[row][col] = false;
        }
    }
}

// Keeps highlight tracking lined up with content that ssd1306_scroll_up()
// just shifted by one text line - otherwise a highlighted cell's tracking
// would stay at its old grid position while the actual (still-inverted)
// pixels moved up with everything else, and a later dehighlight_all() would
// un-invert the wrong cell.
static void scroll_highlight_tracking_up_one_line(void)
{
    for (int row = 0; row < SSD1306_CONSOLE_ROWS - 1; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_COLS; col++) {
            s_highlighted[row][col] = s_highlighted[row + 1][col];
        }
    }
    for (int col = 0; col < SSD1306_CONSOLE_COLS; col++) {
        s_highlighted[SSD1306_CONSOLE_ROWS - 1][col] = false;
    }
}

void ssd1306_console_clear(void)
{
    ssd1306_clear();
    ssd1306_show();
    s_cursor_x = 0;
    s_cursor_y = 0;
    clear_highlight_tracking();
}

void ssd1306_console_set_auto_newline(bool enabled)
{
    s_auto_newline = enabled;
}

void ssd1306_console_new_line(void)
{
    s_cursor_x = 0;

    int next_y = s_cursor_y + SSD1306_CHAR_HEIGHT;
    if (next_y + SSD1306_CHAR_HEIGHT > SSD1306_HEIGHT) {
        // The next line wouldn't fully fit - scroll existing content up by
        // one line instead, and stay on this (now blank) bottom line, same
        // as a normal terminal.
        ssd1306_scroll_up(SSD1306_CHAR_HEIGHT);
        scroll_highlight_tracking_up_one_line();
        ssd1306_show(); // visible immediately, regardless of what the caller does next
    } else {
        s_cursor_y = next_y;
    }
}

void ssd1306_console_write(const char *text)
{
    char one_char[2] = {0, 0};
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == '\n') {
            ssd1306_console_new_line();
            continue;
        }
        if (c == '\r') {
            s_cursor_x = 0;
            continue;
        }
        if (c == '\b') {
            if (s_cursor_x >= SSD1306_CHAR_WIDTH) {
                s_cursor_x -= SSD1306_CHAR_WIDTH;
                ssd1306_draw_text(s_cursor_x, s_cursor_y, " "); // erase the cell
            }
            continue;
        }

        if (s_auto_newline && s_cursor_x + SSD1306_CHAR_WIDTH > SSD1306_WIDTH) {
            ssd1306_console_new_line();
        }
        one_char[0] = c;
        ssd1306_draw_text(s_cursor_x, s_cursor_y, one_char);
        s_cursor_x += SSD1306_CHAR_WIDTH;
    }
    ssd1306_show();
}

void ssd1306_console_write_ln(const char *text)
{
    ssd1306_console_write(text);
    ssd1306_console_new_line();
}

// Inverts the cell (if not already highlighted) and marks it. Idempotent by
// design - the caller may highlight the same cell any number of times.
static void highlight_cell(int col, int row)
{
    if (col < 0 || col >= SSD1306_CONSOLE_COLS || row < 0 || row >= SSD1306_CONSOLE_ROWS) return;
    if (s_highlighted[row][col]) return;
    s_highlighted[row][col] = true;
    // Full cell height, shifted 1px up (see SSD1306_CONSOLE_HIGHLIGHT_Y_OFFSET)
    // so the highlight's own bottom row lands on the gap row instead of the
    // cell's last text row, leaving a 1px unhighlighted border under the
    // text - readability, at the cost of eating into the row above instead.
    // Must match ssd1306_console_dehighlight_all()'s invert exactly, or the
    // two XORs don't cancel out and a row of pixels is left stuck inverted.
    ssd1306_invert_rect(col * SSD1306_CHAR_WIDTH, row * SSD1306_CHAR_HEIGHT + SSD1306_CONSOLE_HIGHLIGHT_Y_OFFSET,
                         SSD1306_CHAR_WIDTH, SSD1306_CHAR_HEIGHT);
}

void ssd1306_console_highlight_char(int col, int row)
{
    highlight_cell(col, row);
    ssd1306_show();
}

void ssd1306_console_highlight_rect(int col, int row, int cols, int rows)
{
    if (cols <= 0 || rows <= 0) return;

    for (int r = row; r < row + rows; r++) {
        for (int c = col; c < col + cols; c++) {
            highlight_cell(c, r);
        }
    }
    ssd1306_show();
}

void ssd1306_console_dehighlight_all(void)
{
    for (int row = 0; row < SSD1306_CONSOLE_ROWS; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_COLS; col++) {
            if (s_highlighted[row][col]) {
                ssd1306_invert_rect(col * SSD1306_CHAR_WIDTH,
                                     row * SSD1306_CHAR_HEIGHT + SSD1306_CONSOLE_HIGHLIGHT_Y_OFFSET,
                                     SSD1306_CHAR_WIDTH, SSD1306_CHAR_HEIGHT);
                s_highlighted[row][col] = false;
            }
        }
    }
    ssd1306_show();
}
