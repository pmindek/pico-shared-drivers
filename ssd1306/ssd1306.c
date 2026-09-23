#include "ssd1306.h"
#include "adafruit_5x7.h"
#include "chonk_7x11.h"
#include "mini_6x7.h"
#include "narrow_7x11.h"
#include "micro_4x7.h"
#include "big_10x12.h"
#include "cp437.h"
#include "main_menu_37x27.h"
#include "main_menu_32x27.h"
#include <string.h>
#include <stdlib.h>

static i2c_inst_t *s_i2c;
static uint8_t s_addr;

// i2c_write_blocking() has no timeout on this SDK: against a panel that
// never ACKs - unplugged, mis-wired, or moved to different pins - it blocks
// forever. That used to happen inside ssd1306_init(), i.e. before any of the
// app existed, so an unplugged display presented as a completely dead board
// with no console output and nothing to go on. Every write here is bounded
// now, and a missing panel is reported instead. Same value and reasoning as
// mpr121.c/aw9523.c.
#define SSD1306_I2C_TIMEOUT_US 50000

// False until a panel has answered, and again once one stops answering. It
// gates every write, so a board with no display runs the whole app at full
// speed with the screen simply absent, rather than paying a timeout per
// write forever.
static bool s_present = false;

// A full frame is 8 page writes. Without this, a panel that dies mid-session
// would cost 8 timeouts - nearly half a second - on every single frame, which
// looks exactly like a hung app even though nothing is stuck. One failed
// write abandons the rest of that frame, and a few failed frames in a row
// give up on the panel until someone calls ssd1306_init() again.
static int s_consecutive_failures = 0;
#define SSD1306_MAX_CONSECUTIVE_FAILURES 5

// 1 bit per pixel, paged: 8 vertical pages of 128 columns each. Always the
// display's own framebuffer specifically - ssd1306_show() pushes this and
// only this, regardless of the current draw target (see s_target below).
static uint8_t s_buf[SSD1306_WIDTH * SSD1306_HEIGHT / 8];
static ssd1306_canvas_t s_display_canvas = {SSD1306_WIDTH, SSD1306_HEIGHT, s_buf};

// What ssd1306_clear()/draw_pixel()/invert_rect()/scroll_up() actually
// operate on - the display by default, or an off-screen canvas between a
// ssd1306_set_target(canvas) and the matching ssd1306_set_target(NULL).
static ssd1306_canvas_t *s_target = &s_display_canvas;

void ssd1306_set_target(ssd1306_canvas_t *canvas) {
    s_target = canvas ? canvas : &s_display_canvas;
}

int ssd1306_target_width(void) {
    return s_target->width;
}

int ssd1306_target_height(void) {
    return s_target->height;
}

bool ssd1306_target_is_display(void) {
    return s_target == &s_display_canvas;
}

static bool s_checkerboard_debug = false;

void ssd1306_set_checkerboard_debug(bool enabled) {
    s_checkerboard_debug = enabled;
}

// The two core pixel-write choke points (ssd1306_draw_pixel(),
// ssd1306_invert_rect()) each check this before touching the target buffer.
static inline bool checkerboard_allows(int x, int y) {
    return !s_checkerboard_debug || ((x ^ y) & 1) == 0;
}

// The one place bytes actually go out. Bypasses s_present on purpose, so
// ssd1306_init() can probe a panel that isn't marked present yet.
static bool ssd1306_write(const uint8_t *buf, size_t len) {
    return i2c_write_timeout_us(s_i2c, s_addr, buf, len, false, SSD1306_I2C_TIMEOUT_US) == (int)len;
}

static void ssd1306_note_failure(void) {
    if (++s_consecutive_failures < SSD1306_MAX_CONSECUTIVE_FAILURES) return;
    s_present = false;
}

static bool ssd1306_cmd(uint8_t cmd) {
    if (!s_present) return false;
    uint8_t buf[2] = {0x00, cmd}; // control byte 0x00 = command
    if (ssd1306_write(buf, 2)) return true;
    ssd1306_note_failure();
    return false;
}

bool ssd1306_init(i2c_inst_t *i2c, uint8_t addr) {
    s_i2c = i2c;
    s_addr = addr;
    s_present = false;
    s_consecutive_failures = 0;

    // Probe before anything else: one command, and if the panel doesn't ACK
    // it, stop here. Costs a single bounded timeout instead of one per
    // command, and it's what turns "no display" from a hang into a return
    // value the caller can report.
    const uint8_t probe[2] = {0x00, 0xAE}; // display off - harmless either way
    if (!ssd1306_write(probe, 2)) return false;
    s_present = true;

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

    // Checked, not fired and forgotten: a panel that ACKs its address but
    // drops a configuration write comes up in a half-set-up state that looks
    // like a hardware fault (wrong size, scrambled, inverted). Same lesson
    // as mpr121_init()'s checked writes.
    bool ok = true;
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ok = ssd1306_cmd(init_cmds[i]) && ok;
    }
    if (!ok) {
        s_present = false;
        return false;
    }

    ssd1306_clear();
    return ssd1306_show();
}

bool ssd1306_is_present(void) {
    return s_present;
}

void ssd1306_clear(void) {
    memset(s_target->buf, 0, SSD1306_CANVAS_BYTES(s_target->width, s_target->height));
}

void ssd1306_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= s_target->width || y < 0 || y >= s_target->height) return;
    if (!checkerboard_allows(x, y)) return;
    size_t idx = x + (y / 8) * s_target->width;
    uint8_t mask = 1 << (y % 8);
    if (on) {
        s_target->buf[idx] |= mask;
    } else {
        s_target->buf[idx] &= ~mask;
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
        if (j < 0 || j >= s_target->height) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= s_target->width) continue;
            if (!checkerboard_allows(i, j)) continue;
            size_t idx = i + (j / 8) * s_target->width;
            uint8_t mask = 1 << (j % 8);
            s_target->buf[idx] ^= mask;
        }
    }
}

void ssd1306_blit_canvas(const ssd1306_canvas_t *src, int src_x, int src_y, int w, int h, int dst_x, int dst_y) {
    for (int j = 0; j < h; j++) {
        int sy = src_y + j;
        for (int i = 0; i < w; i++) {
            int sx = src_x + i;
            bool on = false;
            if (sx >= 0 && sx < src->width && sy >= 0 && sy < src->height) {
                size_t idx = sx + (sy / 8) * src->width;
                uint8_t mask = 1 << (sy % 8);
                on = (src->buf[idx] & mask) != 0;
            }
            ssd1306_draw_pixel(dst_x + i, dst_y + j, on);
        }
    }
}

const ssd1306_font_t ssd1306_font_adafruit = {
    .glyph_width = 5,
    .glyph_height = 8,
    .glyphs = kFontAdafruit5x7,
    .first_code = 0,
    .glyph_count = 256,
};

const ssd1306_font_t ssd1306_font_chonk = {
    .glyph_width = 7,
    .glyph_height = 11,
    .glyphs = kFontChonk7x11,
    .first_code = 32,
    .glyph_count = 95,
};

const ssd1306_font_t ssd1306_font_mini = {
    .glyph_width = 6,
    .glyph_height = 7,
    .glyphs = kFontMini6x7,
    .first_code = 32,
    .glyph_count = 96,
};

const ssd1306_font_t ssd1306_font_narrow = {
    .glyph_width = 7,
    .glyph_height = 11,
    .glyphs = kFontNarrow7x11,
    .first_code = 32,
    .glyph_count = 95,
};

const ssd1306_font_t ssd1306_font_micro = {
    .glyph_width = 4,
    .glyph_height = 7,
    .glyphs = kFontMicro4x7,
    .first_code = 32,
    .glyph_count = 95,
};

const ssd1306_font_t ssd1306_font_big = {
    .glyph_width = 10,
    .glyph_height = 12,
    .glyphs = kFontBig10x12,
    .first_code = 32,
    .glyph_count = 95,
};

const ssd1306_font_t ssd1306_font_cp437 = {
    .glyph_width = 9,
    .glyph_height = 16,
    .glyphs = kFontCp437,
    .first_code = 0,
    .glyph_count = 256,
};

const ssd1306_font_t ssd1306_font_main_menu_32 = {
    .glyph_width = 32,
    .glyph_height = 27,
    .glyphs = kFontMainMenu32x27,
    .first_code = 32,
    .glyph_count = 5,
    .ui_only = true,
};

const ssd1306_font_t ssd1306_font_main_menu = {
    .glyph_width = 37,
    .glyph_height = 27,
    .glyphs = kFontMainMenu37x27,
    .first_code = 32,
    .glyph_count = 3,
    .ui_only = true,
};

void ssd1306_draw_text(int x, int y, const char *text, const ssd1306_font_t *font, int spacing_px) {
    int bytes_per_col = (font->glyph_height + 7) / 8;
    int advance = font->glyph_width + spacing_px;

    for (int i = 0; text[i] != '\0'; i++) {
        uint8_t code = (uint8_t)text[i];
        bool has_glyph = code >= font->first_code && code - font->first_code < font->glyph_count;
        const uint8_t *glyph =
            has_glyph ? &font->glyphs[(code - font->first_code) * font->glyph_width * bytes_per_col] : NULL;

        // Paint every pixel in the cell - on AND off, including the spacing
        // gap - not just the "on" ones. Otherwise redrawing a different
        // (e.g. narrower, or blank) character over an existing one leaves
        // ghost pixels behind, which is exactly what happens if a caller
        // relies on this to erase (e.g. backspace). A code outside the
        // font's range (has_glyph false) just paints the whole cell blank,
        // same as any other "off" pixel.
        for (int col = 0; col < advance; col++) {
            const uint8_t *col_bytes = (glyph && col < font->glyph_width) ? &glyph[col * bytes_per_col] : NULL;
            for (int row = 0; row < font->glyph_height; row++) {
                bool on = col_bytes && (col_bytes[row / 8] & (1 << (row % 8))) != 0;
                ssd1306_draw_pixel(x + i * advance + col, y + row, on);
            }
        }
    }
}

void ssd1306_scroll_up(int rows) {
    if (rows <= 0) return;
    if (rows >= s_target->height) {
        ssd1306_clear();
        return;
    }
    // General pixel-by-pixel shift (not a memmove of whole pages) so this
    // stays correct even if a scroll amount isn't a multiple of the 8-row
    // page size the buffer happens to be organized in.
    for (int x = 0; x < s_target->width; x++) {
        for (int y = 0; y < s_target->height; y++) {
            int src_y = y + rows;
            bool on = false;
            if (src_y < s_target->height) {
                size_t src_idx = x + (src_y / 8) * s_target->width;
                uint8_t src_mask = 1 << (src_y % 8);
                on = (s_target->buf[src_idx] & src_mask) != 0;
            }
            size_t dst_idx = x + (y / 8) * s_target->width;
            uint8_t dst_mask = 1 << (y % 8);
            if (on) {
                s_target->buf[dst_idx] |= dst_mask;
            } else {
                s_target->buf[dst_idx] &= ~dst_mask;
            }
        }
    }
}

void ssd1306_set_display_on(bool on) {
    ssd1306_cmd(on ? 0xAF : 0xAE);
}

bool ssd1306_show(void) {
    if (!s_present) return false; // no panel: drawing still works, it just goes nowhere

    // Constrain the column/page range so the whole 128x64 buffer gets pushed.
    if (!ssd1306_cmd(0x21)) return false; // set column address
    if (!ssd1306_cmd(0)) return false;
    if (!ssd1306_cmd(SSD1306_WIDTH - 1)) return false;
    if (!ssd1306_cmd(0x22)) return false; // set page address
    if (!ssd1306_cmd(0)) return false;
    if (!ssd1306_cmd((SSD1306_HEIGHT / 8) - 1)) return false;

    uint8_t tx[SSD1306_WIDTH + 1];
    tx[0] = 0x40; // control byte: data
    for (int page = 0; page < SSD1306_HEIGHT / 8; page++) {
        memcpy(&tx[1], &s_buf[page * SSD1306_WIDTH], SSD1306_WIDTH);
        if (!ssd1306_write(tx, SSD1306_WIDTH + 1)) {
            // Abandon the rest of the frame rather than paying a timeout per
            // remaining page - see s_consecutive_failures.
            ssd1306_note_failure();
            return false;
        }
    }

    s_consecutive_failures = 0;
    return true;
}
