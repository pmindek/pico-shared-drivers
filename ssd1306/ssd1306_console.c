#include "ssd1306_console.h"
#include "ssd1306.h"

static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_auto_newline = true;

void ssd1306_console_clear(void)
{
    ssd1306_clear();
    ssd1306_show();
    s_cursor_x = 0;
    s_cursor_y = 0;
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
