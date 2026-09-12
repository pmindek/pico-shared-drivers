#include "ssd1306.h"
#include "font5x7.h"
#include <string.h>
#include <stdlib.h>

static i2c_inst_t *s_i2c;
static uint8_t s_addr;

// 1 bit per pixel, paged: 8 vertical pages of 128 columns each.
static uint8_t s_buf[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

static void ssd1306_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // control byte 0x00 = command
    i2c_write_blocking(s_i2c, s_addr, buf, 2, false);
}

void ssd1306_init(i2c_inst_t *i2c, uint8_t addr) {
    s_i2c = i2c;
    s_addr = addr;

    static const uint8_t init_cmds[] = {
        0xAE,       // display off
        0x20, 0x00, // memory addressing mode = horizontal
        0xB0,       // page start address 0
        0xC8,       // COM output scan direction, remapped
        0x00,       // low column address
        0x10,       // high column address
        0x40,       // start line = 0
        0x81, 0x7F, // contrast
        0xA1,       // segment re-map
        0xA6,       // normal (not inverted) display
        0xA8, 0x3F, // multiplex ratio = 63 (64 rows)
        0xA4,       // resume to RAM content display
        0xD3, 0x00, // display offset = 0
        0xD5, 0x80, // display clock divide ratio / osc freq
        0xD9, 0xF1, // pre-charge period
        0xDA, 0x12, // COM pins hardware config (128x64)
        0xDB, 0x40, // VCOMH deselect level
        0x8D, 0x14, // charge pump enable
        0xAF,       // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ssd1306_cmd(init_cmds[i]);
    }

    ssd1306_clear();
    ssd1306_show();
}

void ssd1306_clear(void) {
    memset(s_buf, 0, sizeof(s_buf));
}

void ssd1306_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    size_t idx = x + (y / 8) * SSD1306_WIDTH;
    uint8_t mask = 1 << (y % 8);
    if (on) {
        s_buf[idx] |= mask;
    } else {
        s_buf[idx] &= ~mask;
    }
}

// Bresenham line algorithm.
void ssd1306_draw_line(int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        ssd1306_draw_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Midpoint circle algorithm.
void ssd1306_draw_circle(int cx, int cy, int r, bool on) {
    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        ssd1306_draw_pixel(cx + x, cy + y, on);
        ssd1306_draw_pixel(cx + y, cy + x, on);
        ssd1306_draw_pixel(cx - y, cy + x, on);
        ssd1306_draw_pixel(cx - x, cy + y, on);
        ssd1306_draw_pixel(cx - x, cy - y, on);
        ssd1306_draw_pixel(cx - y, cy - x, on);
        ssd1306_draw_pixel(cx + y, cy - x, on);
        ssd1306_draw_pixel(cx + x, cy - y, on);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void ssd1306_fill_circle(int cx, int cy, int r, bool on) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                ssd1306_draw_pixel(cx + x, cy + y, on);
            }
        }
    }
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            ssd1306_draw_pixel(i, j, on);
        }
    }
}

void ssd1306_draw_text(int x, int y, const char *text) {
    for (int i = 0; text[i] != '\0'; i++) {
        const uint8_t *glyph = &kFont5x7Cp437[(uint8_t)text[i] * 5];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < SSD1306_CHAR_HEIGHT; row++) {
                if (bits & (1 << row)) {
                    ssd1306_draw_pixel(x + i * SSD1306_CHAR_WIDTH + col, y + row, true);
                }
            }
        }
    }
}

void ssd1306_show(void) {
    // Constrain the column/page range so the whole 128x64 buffer gets pushed.
    ssd1306_cmd(0x21); // set column address
    ssd1306_cmd(0);
    ssd1306_cmd(SSD1306_WIDTH - 1);
    ssd1306_cmd(0x22); // set page address
    ssd1306_cmd(0);
    ssd1306_cmd((SSD1306_HEIGHT / 8) - 1);

    uint8_t tx[SSD1306_WIDTH + 1];
    tx[0] = 0x40; // control byte: data
    for (int page = 0; page < SSD1306_HEIGHT / 8; page++) {
        memcpy(&tx[1], &s_buf[page * SSD1306_WIDTH], SSD1306_WIDTH);
        i2c_write_blocking(s_i2c, s_addr, tx, SSD1306_WIDTH + 1, false);
    }
}
