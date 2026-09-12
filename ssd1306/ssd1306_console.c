#include "ssd1306_console.h"
#include "ssd1306.h"

static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_auto_newline = true;

// Highlight tracking, one flag per character cell. Rounds up so a partial
// trailing cell (e.g. SSD1306_WIDTH not a multiple of SSD1306_CHAR_WIDTH)
// still has a slot.
#define CONSOLE_COLS ((SSD1306_WIDTH + SSD1306_CHAR_WIDTH - 1) / SSD1306_CHAR_WIDTH)
#define CONSOLE_ROWS ((SSD1306_HEIGHT + SSD1306_CHAR_HEIGHT - 1) / SSD1306_CHAR_HEIGHT)
static bool s_highlighted[CONSOLE_ROWS][CONSOLE_COLS];

static void clear_highlight_tracking(void)
{
    for (int row = 0; row < CONSOLE_ROWS; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
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
    for (int row = 0; row < CONSOLE_ROWS - 1; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            s_highlighted[row][col] = s_highlighted[row + 1][col];
        }
    }
    for (int col = 0; col < CONSOLE_COLS; col++) {
        s_highlighted[CONSOLE_ROWS - 1][col] = false;
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
    if (col < 0 || col >= CONSOLE_COLS || row < 0 || row >= CONSOLE_ROWS) return;
    if (s_highlighted[row][col]) return;
    s_highlighted[row][col] = true;
    ssd1306_invert_rect(col * SSD1306_CHAR_WIDTH, row * SSD1306_CHAR_HEIGHT, SSD1306_CHAR_WIDTH, SSD1306_CHAR_HEIGHT);
}

void ssd1306_console_highlight_char(int x, int y)
{
    highlight_cell(x / SSD1306_CHAR_WIDTH, y / SSD1306_CHAR_HEIGHT);
    ssd1306_show();
}

void ssd1306_console_highlight_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;

    int col_start = x / SSD1306_CHAR_WIDTH;
    int col_end = (x + w - 1) / SSD1306_CHAR_WIDTH;
    int row_start = y / SSD1306_CHAR_HEIGHT;
    int row_end = (y + h - 1) / SSD1306_CHAR_HEIGHT;

    for (int row = row_start; row <= row_end; row++) {
        for (int col = col_start; col <= col_end; col++) {
            highlight_cell(col, row);
        }
    }
    ssd1306_show();
}

void ssd1306_console_dehighlight_all(void)
{
    for (int row = 0; row < CONSOLE_ROWS; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            if (s_highlighted[row][col]) {
                ssd1306_invert_rect(col * SSD1306_CHAR_WIDTH, row * SSD1306_CHAR_HEIGHT, SSD1306_CHAR_WIDTH,
                                     SSD1306_CHAR_HEIGHT);
                s_highlighted[row][col] = false;
            }
        }
    }
    ssd1306_show();
}
