#include "ssd1306_console.h"
#include "ssd1306.h"

static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_auto_newline = true;

static const ssd1306_font_t *s_font = &ssd1306_font_adafruit;
static int s_char_spacing = 1;
static int s_line_spacing = 1;

// See ssd1306_console_set_highlight_offset()'s doc comment for why this
// defaults to (0, -1): a deliberate 1px unhighlighted border under
// highlighted text, landing on the gap row below it instead of the text's
// own last row.
static int s_highlight_offset_x = 0;
static int s_highlight_offset_y = -1;

static inline int cell_width(void) { return s_font->glyph_width + s_char_spacing; }
static inline int cell_height(void) { return s_font->glyph_height + s_line_spacing; }

// Highlight tracking is a fixed-size array, sized for the smallest
// font/spacing combination worth using in practice, to avoid dynamic
// allocation - a cell size small enough to want more than this many
// columns/rows is clamped instead of indexed out of bounds (see
// ssd1306_console_cols()/rows() and highlight_cell()'s own bounds check).
// MAX_COLS is sized for the display's own width (the narrowest realistic
// draw target - an off-screen canvas is never wider); MAX_ROWS is sized
// generously beyond the display's own height (SSD1306_HEIGHT / 3 = 21)
// since the console can also draw into a window's content canvas, which is
// commonly much taller than the display to hold scrollable content.
#define SSD1306_CONSOLE_MAX_COLS 42 // SSD1306_WIDTH / 3
#define SSD1306_CONSOLE_MAX_ROWS 40
static bool s_highlighted[SSD1306_CONSOLE_MAX_ROWS][SSD1306_CONSOLE_MAX_COLS];

void ssd1306_console_set_font(const ssd1306_font_t *font)
{
    s_font = font;
}

void ssd1306_console_set_char_spacing(int spacing_px)
{
    s_char_spacing = spacing_px;
}

void ssd1306_console_set_line_spacing(int spacing_px)
{
    s_line_spacing = spacing_px;
}

void ssd1306_console_set_highlight_offset(int dx, int dy)
{
    s_highlight_offset_x = dx;
    s_highlight_offset_y = dy;
}

static inline int clamp_cols(int cols) { return cols > SSD1306_CONSOLE_MAX_COLS ? SSD1306_CONSOLE_MAX_COLS : cols; }
static inline int clamp_rows(int rows) { return rows > SSD1306_CONSOLE_MAX_ROWS ? SSD1306_CONSOLE_MAX_ROWS : rows; }

int ssd1306_console_cols(void)
{
    int cw = cell_width();
    return clamp_cols((ssd1306_target_width() + cw - 1) / cw);
}

int ssd1306_console_rows(void)
{
    int ch = cell_height();
    return clamp_rows((ssd1306_target_height() + ch - 1) / ch);
}

int ssd1306_console_cell_width(void)
{
    return cell_width();
}

int ssd1306_console_cell_height(void)
{
    return cell_height();
}

static void clear_highlight_tracking(void)
{
    for (int row = 0; row < SSD1306_CONSOLE_MAX_ROWS; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_MAX_COLS; col++) {
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
    for (int row = 0; row < SSD1306_CONSOLE_MAX_ROWS - 1; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_MAX_COLS; col++) {
            s_highlighted[row][col] = s_highlighted[row + 1][col];
        }
    }
    for (int col = 0; col < SSD1306_CONSOLE_MAX_COLS; col++) {
        s_highlighted[SSD1306_CONSOLE_MAX_ROWS - 1][col] = false;
    }
}

void ssd1306_console_clear(void)
{
    ssd1306_clear();
    if (ssd1306_target_is_display()) ssd1306_show();
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

    int ch = cell_height();
    int next_y = s_cursor_y + ch;
    if (next_y + ch > ssd1306_target_height()) {
        // The next line wouldn't fully fit - scroll existing content up by
        // one line instead, and stay on this (now blank) bottom line, same
        // as a normal terminal.
        ssd1306_scroll_up(ch);
        scroll_highlight_tracking_up_one_line();
        if (ssd1306_target_is_display()) ssd1306_show(); // visible immediately, regardless of what the caller does next
    } else {
        s_cursor_y = next_y;
    }
}

void ssd1306_console_write(const char *text)
{
    char one_char[2] = {0, 0};
    int cw = cell_width();
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
            if (s_cursor_x >= cw) {
                s_cursor_x -= cw;
                ssd1306_draw_text(s_cursor_x, s_cursor_y, " ", s_font, s_char_spacing); // erase the cell
            }
            continue;
        }

        if (s_auto_newline && s_cursor_x + cw > ssd1306_target_width()) {
            ssd1306_console_new_line();
        }
        one_char[0] = c;
        ssd1306_draw_text(s_cursor_x, s_cursor_y, one_char, s_font, s_char_spacing);
        s_cursor_x += cw;
    }
    if (ssd1306_target_is_display()) ssd1306_show();
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
    if (col < 0 || col >= SSD1306_CONSOLE_MAX_COLS || row < 0 || row >= SSD1306_CONSOLE_MAX_ROWS) return;
    if (s_highlighted[row][col]) return;
    s_highlighted[row][col] = true;
    // Must match ssd1306_console_dehighlight_all()'s invert exactly (same
    // offset, same size), or the two XORs don't cancel out and a row of
    // pixels is left stuck inverted.
    int cw = cell_width(), ch = cell_height();
    ssd1306_invert_rect(col * cw + s_highlight_offset_x, row * ch + s_highlight_offset_y, cw, ch);
}

void ssd1306_console_highlight_char(int col, int row)
{
    highlight_cell(col, row);
    if (ssd1306_target_is_display()) ssd1306_show();
}

void ssd1306_console_highlight_rect(int col, int row, int cols, int rows)
{
    if (cols <= 0 || rows <= 0) return;

    for (int r = row; r < row + rows; r++) {
        for (int c = col; c < col + cols; c++) {
            highlight_cell(c, r);
        }
    }
    if (ssd1306_target_is_display()) ssd1306_show();
}

void ssd1306_console_dehighlight_all(void)
{
    int cw = cell_width(), ch = cell_height();
    for (int row = 0; row < SSD1306_CONSOLE_MAX_ROWS; row++) {
        for (int col = 0; col < SSD1306_CONSOLE_MAX_COLS; col++) {
            if (s_highlighted[row][col]) {
                ssd1306_invert_rect(col * cw + s_highlight_offset_x, row * ch + s_highlight_offset_y, cw, ch);
                s_highlighted[row][col] = false;
            }
        }
    }
    if (ssd1306_target_is_display()) ssd1306_show();
}
