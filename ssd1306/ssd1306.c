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

void ssd1306_invert_rect(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= SSD1306_HEIGHT) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= SSD1306_WIDTH) continue;
            size_t idx = i + (j / 8) * SSD1306_WIDTH;
            uint8_t mask = 1 << (j % 8);
            s_buf[idx] ^= mask;
        }
    }
}

typedef struct {
    uint8_t code;
    uint16_t columns[SSD1306_CHAR_WIDTH]; // bit N = row N (bit 8 = the gap row)
} box_glyph_t;

// The double-line box-drawing characters (see ssd1306_console.h's
// SSD1306_CONSOLE_BOX_*) get their own full-cell glyphs, filling
// SSD1306_CHAR_WIDTH x SSD1306_CHAR_HEIGHT (6x9) instead of font5x7's
// regular 5x8, so adjacent box characters form continuous lines instead of
// a dotted one with a 1px gap between every character. font5x7.h itself
// stays untouched (a byte-verified public-domain source) - these are
// separate, purpose-built glyphs, not derived from its existing
// 0xC9/0xBB/0xC8/0xBC/0xCD/0xBA entries (an earlier version tried extending
// those, but they have their own internal asymmetric notches that made
// that approach keep reproducing gaps/stray pixels).
//
// Built instead from two simple rules, verified by rendering a full
// simulated box (corner+H+H+H+corner, and V stacked on blank on V), not
// just individual glyphs in isolation:
//   - a solid double horizontal line at rows 2 and 4 (matching H exactly),
//     spanning columns 2 through 5 for left corners (5 = the gap column, so
//     it continues seamlessly into a following H) or 0 through 4 for right
//     corners (nothing follows those here, so no gap-column fill)
//   - a solid double vertical line at columns 2 and 4, spanning rows 2-8
//     for top corners (continues down into a V on the next line) or rows
//     0-4 for bottom corners (receives a V from the line above)
// The two rules' pixels simply overlap where they cross - no separate
// corner-junction case needed.
static const box_glyph_t kBoxGlyphs[] = {
    {0xCD, {0x014, 0x014, 0x014, 0x014, 0x014, 0x014}}, // horizontal
    {0xBA, {0x000, 0x000, 0x1FF, 0x000, 0x1FF, 0x000}}, // vertical
    {0xC9, {0x000, 0x000, 0x1FC, 0x014, 0x1FC, 0x014}}, // top-left
    {0xBB, {0x014, 0x014, 0x1FC, 0x014, 0x1FC, 0x000}}, // top-right
    {0xC8, {0x000, 0x000, 0x01F, 0x014, 0x01F, 0x014}}, // bottom-left
    {0xBC, {0x014, 0x014, 0x01F, 0x014, 0x01F, 0x000}}, // bottom-right
};

static const uint16_t *find_box_glyph(uint8_t code) {
    for (int i = 0; i < (int)(sizeof(kBoxGlyphs) / sizeof(kBoxGlyphs[0])); i++) {
        if (kBoxGlyphs[i].code == code) return kBoxGlyphs[i].columns;
    }
    return NULL;
}

void ssd1306_draw_text(int x, int y, const char *text) {
    for (int i = 0; text[i] != '\0'; i++) {
        const uint16_t *box = find_box_glyph((uint8_t)text[i]);
        const uint8_t *glyph = box ? NULL : &kFont5x7Cp437[(uint8_t)text[i] * 5];
        // Paint every pixel in the cell - on AND off, including the 1px
        // gap column/row - not just the "on" ones. Otherwise redrawing a
        // different (e.g. narrower, or blank) character over an existing
        // one leaves ghost pixels behind, which is exactly what happens if
        // a caller relies on this to erase (e.g. backspace).
        for (int col = 0; col < SSD1306_CHAR_WIDTH; col++) {
            uint16_t bits = box ? box[col] : (col < 5 ? glyph[col] : 0);
            for (int row = 0; row < SSD1306_CHAR_HEIGHT; row++) {
                bool on = (bits & (1 << row)) != 0;
                ssd1306_draw_pixel(x + i * SSD1306_CHAR_WIDTH + col, y + row, on);
            }
        }
    }
}

void ssd1306_scroll_up(int rows) {
    if (rows <= 0) return;
    if (rows >= SSD1306_HEIGHT) {
        ssd1306_clear();
        return;
    }
    // General pixel-by-pixel shift (not a memmove of whole pages) so this
    // stays correct even if a scroll amount isn't a multiple of the 8-row
    // page size the buffer happens to be organized in.
    for (int x = 0; x < SSD1306_WIDTH; x++) {
        for (int y = 0; y < SSD1306_HEIGHT; y++) {
            int src_y = y + rows;
            bool on = false;
            if (src_y < SSD1306_HEIGHT) {
                size_t src_idx = x + (src_y / 8) * SSD1306_WIDTH;
                uint8_t src_mask = 1 << (src_y % 8);
                on = (s_buf[src_idx] & src_mask) != 0;
            }
            size_t dst_idx = x + (y / 8) * SSD1306_WIDTH;
            uint8_t dst_mask = 1 << (y % 8);
            if (on) {
                s_buf[dst_idx] |= dst_mask;
            } else {
                s_buf[dst_idx] &= ~dst_mask;
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
