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
    s_cursor_y += SSD1306_CHAR_HEIGHT;
    if (s_cursor_y + SSD1306_CHAR_HEIGHT > SSD1306_HEIGHT) {
        s_cursor_y = 0; // no scrolling - wrap back to the top
    }
}

void ssd1306_console_write(const char *text)
{
    char one_char[2] = {0, 0};
    for (int i = 0; text[i] != '\0'; i++) {
        if (s_auto_newline && s_cursor_x + SSD1306_CHAR_WIDTH > SSD1306_WIDTH) {
            ssd1306_console_new_line();
        }
        one_char[0] = text[i];
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
