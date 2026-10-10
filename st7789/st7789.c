#include "st7789.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

// Raw glyph-bitmap data only (no struct, no i2c) - the exact same headers
// ssd1306.c includes for its own font wiring. See st7789.h's own comment
// on why the data is shared but the ssd1306_font_t/st7789_font_t *types*
// deliberately aren't.
#include "../ssd1306/adafruit_5x7.h"
#include "../ssd1306/chonk_7x11.h"
#include "../ssd1306/mini_6x7.h"
#include "../ssd1306/narrow_7x11.h"
#include "../ssd1306/micro_4x7.h"
#include "../ssd1306/big_10x12.h"
#include "../ssd1306/tall_9x13.h"

// ---- ST7789 command opcodes ----
// Standard across every ST7789 revision (V1/V2/V3 - the suffix is a
// manufacturing trim, not a command-set change, as far as every library
// and datasheet variant surveyed for this driver agrees) and identical to
// what Adafruit's and TFT_eSPI's own ST7789 drivers use.
#define ST7789_CMD_SWRESET 0x01
#define ST7789_CMD_SLPOUT 0x11
#define ST7789_CMD_NORON 0x13
#define ST7789_CMD_INVOFF 0x20
#define ST7789_CMD_INVON 0x21
#define ST7789_CMD_DISPON 0x29
#define ST7789_CMD_CASET 0x2A
#define ST7789_CMD_RASET 0x2B
#define ST7789_CMD_RAMWR 0x2C
#define ST7789_CMD_MADCTL 0x36
#define ST7789_CMD_COLMOD 0x3A

// ---- Panel calibration knobs ----
// Three things vary between otherwise-identical ST7789 breakouts, and none
// of them can be detected in software (see st7789.h's own comment on why
// there's no presence/ID check at all) - each is set to the more common
// choice for a small IPS module of this type, and each has an obvious,
// harmless visual symptom if it's wrong. Fix by flipping the one constant,
// not by guessing at the others.

// Many small IPS ST7789 breakouts are wired BGR rather than RGB internally,
// but this one isn't - confirmed on real hardware (2026-09-24): a filled
// ST7789_RED circle came out blue with kBgrPanel=true, exactly the R/B
// swap this flag exists to correct, so it needs to be off here. Other
// panels can override it on the command line (-DST7789_BGR=1) - birch's
// 240x320 IPS module needs it on (R/B swapped without it, 2026-10-10).
#ifndef ST7789_BGR
#define ST7789_BGR 0
#endif
static const bool kBgrPanel = ST7789_BGR;

// Mirror the picture at the controller (MADCTL MX = left-right, MY =
// top-bottom; both = 180-degree rotation), for a panel whose glass is
// wired differently from the 1.69" module this driver was written for -
// -DST7789_MIRROR_X=1 / -DST7789_MIRROR_Y=1. birch's 240x320 IPS module
// needs MIRROR_Y only (2026-10-10: top-bottom flipped with neither,
// left-right flipped with both). Safe with the row offset: the glass is
// centered in GRAM, so its window is the same rows either way up.
#ifndef ST7789_MIRROR_X
#define ST7789_MIRROR_X 0
#endif
#ifndef ST7789_MIRROR_Y
#define ST7789_MIRROR_Y 0
#endif
static const bool kMirrorX = ST7789_MIRROR_X;
static const bool kMirrorY = ST7789_MIRROR_Y;

// Many IPS ST7789 panels need display inversion ON for correct (non-
// negative) colors - a TN panel usually doesn't. Symptom if wrong: colors
// look like a photo negative.
static const bool kInvertColors = true;

// Confirmed on real hardware (2026-09-24): this panel is exactly this
// case. The controller's RAM is 320 rows tall; the 280-row glass is
// mounted starting 20 rows in, not at row 0 - the common "center a 280-row
// panel in 320 rows of GRAM" layout (20px margin top and bottom). Writing
// to GRAM rows 0-279 (the old default, kRowOffset=0) put the image 20 rows
// higher than the glass shows it - its top 20 rows displayed image rows
// 20+ instead of 0+ (the reported "shifted upward"), and its bottom 20
// rows showed GRAM the driver never wrote to at all (the reported "colored
// noise"). Writing to the glass's own actual window, GRAM rows 20-299,
// fixes both at once. Width matches (240 = 240), so no column offset.
//
// Generalized for other glass heights (st7789.h's ST7789_PANEL_HEIGHT):
// the glass is assumed centered in the 320 GRAM rows - 20 for the 280-row
// glass above (verified), 0 for a full 320-row glass (nothing to center).
// Other layouts (e.g. 240x240 modules, often at 0 or 80 depending on
// orientation) can define ST7789_GRAM_ROW_OFFSET themselves. On top of
// that, a framebuffer shorter than the glass is centered on it.
#ifndef ST7789_GRAM_ROW_OFFSET
#define ST7789_GRAM_ROW_OFFSET ((320 - ST7789_PANEL_HEIGHT) / 2)
#endif
static const int kColOffset = 0;
static const int kRowOffset = ST7789_GRAM_ROW_OFFSET + (ST7789_PANEL_HEIGHT - ST7789_HEIGHT) / 2;

// Conservative starting clock for first bring-up on breadboard jumper
// wires, where wire length/quality (not the chip) is usually the real
// limit. ST7789 write timing allows quite a bit faster (the datasheet's
// SCK cycle spec works out to tens of MHz) - once the wiring is solid
// (especially on a real PCB), this is the first thing worth raising to
// speed up st7789_show(); nothing else here depends on the exact value.
#define ST7789_SPI_HZ (20 * 1000 * 1000)

// Backlight PWM: full 16-bit counter, default (1x) clock divider - lands in
// the low kHz range regardless of the exact system clock (150MHz/65536 is
// ~2.3kHz, and it won't move far from that), comfortably above visible
// flicker without needing to query clk_sys just to pick a divider.
#define BACKLIGHT_PWM_WRAP 65535

// What st7789_init() leaves the backlight at, before any caller touches it.
// Unlike kBgrPanel/kColOffset/kRowOffset above, this isn't a hardware-
// correctness fix - it's this panel's owner's own comfortable-viewing
// brightness on their desk (2026-09-24), a preference rather than a fact
// about the silicon (measured current draw at nearby levels: 24mA at 0%,
// 35mA at 16%, 58mA at 52%, 86mA at 100% - the 24mA floor is the rest of
// the circuit, not the LED; see PINOUT.md/memory for the full readout).
// Any caller can override it with a single st7789_set_backlight_level()
// call right after st7789_init() returns.
static const uint8_t kDefaultBacklightPercent = 20;

static uint16_t s_buf[ST7789_WIDTH * ST7789_HEIGHT]; // row-major, one RGB565 pixel per entry

static spi_inst_t *s_spi;
static uint s_cs_pin, s_dc_pin, s_bl_pin;

// ---- Framebuffer drawing - no hardware I/O below this line until st7789_show() ----

void st7789_clear(uint16_t color)
{
    for (int i = 0; i < ST7789_WIDTH * ST7789_HEIGHT; i++) s_buf[i] = color;
}

void st7789_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= ST7789_WIDTH || y < 0 || y >= ST7789_HEIGHT) return;
    s_buf[y * ST7789_WIDTH + x] = color;
}

uint16_t st7789_get_pixel(int x, int y)
{
    if (x < 0 || x >= ST7789_WIDTH || y < 0 || y >= ST7789_HEIGHT) return 0; // black
    return s_buf[y * ST7789_WIDTH + x];
}

void st7789_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > ST7789_WIDTH) x1 = ST7789_WIDTH;
    if (y1 > ST7789_HEIGHT) y1 = ST7789_HEIGHT;

    for (int j = y0; j < y1; j++) {
        for (int i = x0; i < x1; i++) {
            s_buf[j * ST7789_WIDTH + i] = color;
        }
    }
}

// Midpoint circle algorithm (Bresenham's circle), outline only - 8-way
// symmetry, one octant computed and mirrored. Same technique as
// ssd1306_draw_circle() in the shared ssd1306 driver, just against this
// driver's own pixel setter.
void st7789_draw_circle(int cx, int cy, int r, uint16_t color)
{
    int x = r, y = 0;
    int err = 1 - r;
    while (x >= y) {
        st7789_draw_pixel(cx + x, cy + y, color);
        st7789_draw_pixel(cx + y, cy + x, color);
        st7789_draw_pixel(cx - y, cy + x, color);
        st7789_draw_pixel(cx - x, cy + y, color);
        st7789_draw_pixel(cx - x, cy - y, color);
        st7789_draw_pixel(cx - y, cy - x, color);
        st7789_draw_pixel(cx + y, cy - x, color);
        st7789_draw_pixel(cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

// Same algorithm as st7789_draw_circle(), but each symmetric point pair is
// a horizontal span filled with st7789_fill_rect() instead of two pixels -
// turns the outline into a solid disc with no gaps and no doubled-up
// pixels along the way.
void st7789_fill_circle(int cx, int cy, int r, uint16_t color)
{
    int x = r, y = 0;
    int err = 1 - r;
    while (x >= y) {
        st7789_fill_rect(cx - x, cy + y, 2 * x + 1, 1, color);
        st7789_fill_rect(cx - x, cy - y, 2 * x + 1, 1, color);
        st7789_fill_rect(cx - y, cy + x, 2 * y + 1, 1, color);
        st7789_fill_rect(cx - y, cy - x, 2 * y + 1, 1, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

// ---- Fonts ----
// Same names, same glyph data, same metrics as their ssd1306_font_* twins
// (see st7789.h's own comment on why the data is shared but the struct
// type isn't) - kept in the same order ssd1306.c defines them in, purely
// so the two are easy to compare side by side.
const st7789_font_t st7789_font_adafruit = {5, 8, kFontAdafruit5x7, 0, 256};
const st7789_font_t st7789_font_chonk = {7, 11, kFontChonk7x11, 32, 96};
const st7789_font_t st7789_font_mini = {6, 7, kFontMini6x7, 32, 96};
const st7789_font_t st7789_font_narrow = {7, 11, kFontNarrow7x11, 32, 95};
const st7789_font_t st7789_font_micro = {4, 7, kFontMicro4x7, 32, 95};
const st7789_font_t st7789_font_big = {10, 12, kFontBig10x12, 32, 95};
const st7789_font_t st7789_font_tall = {9, 13, kFontTall9x13, 32, 96};

// Identical column-major decode to ssd1306_draw_text() (see its own
// comment for the full reasoning) - ported, not reinvented, on purpose:
// this bit-unpacking has already been proven against every font here, so
// matching it exactly means there's no new place for an off-by-one to
// creep in. The only real difference is what a lit/unlit pixel becomes -
// fg/bg colors instead of true/false.
void st7789_draw_text(int x, int y, const char *text, const st7789_font_t *font, int spacing_px, uint16_t fg,
                      uint16_t bg)
{
    int bytes_per_col = (font->glyph_height + 7) / 8;
    int advance = font->glyph_width + spacing_px;

    for (int i = 0; text[i] != '\0'; i++) {
        uint8_t code = (uint8_t)text[i];
        bool has_glyph = code >= font->first_code && code - font->first_code < font->glyph_count;
        const uint8_t *glyph =
            has_glyph ? &font->glyphs[(code - font->first_code) * font->glyph_width * bytes_per_col] : NULL;

        for (int col = 0; col < advance; col++) {
            const uint8_t *col_bytes = (glyph && col < font->glyph_width) ? &glyph[col * bytes_per_col] : NULL;
            for (int row = 0; row < font->glyph_height; row++) {
                bool on = col_bytes && (col_bytes[row / 8] & (1 << (row % 8))) != 0;
                st7789_draw_pixel(x + i * advance + col, y + row, on ? fg : bg);
            }
        }
    }
}

// ---- Hardware I/O ----

static void cs_select(void) { gpio_put(s_cs_pin, 0); }
static void cs_deselect(void) { gpio_put(s_cs_pin, 1); }

static void write_cmd(uint8_t cmd)
{
    cs_select();
    gpio_put(s_dc_pin, 0); // command
    spi_write_blocking(s_spi, &cmd, 1);
    cs_deselect();
}

static void write_data(const uint8_t *data, size_t len)
{
    cs_select();
    gpio_put(s_dc_pin, 1); // data
    spi_write_blocking(s_spi, data, len);
    cs_deselect();
}

static void write_data8(uint8_t value) { write_data(&value, 1); }

// Sets the controller's active pixel window to the given rectangle (in
// framebuffer/logical coordinates - kColOffset/kRowOffset are added here,
// callers never add them themselves) - CASET/RASET each take a 16-bit
// start and end column/row, big-endian, one byte pair each. Every caller
// is expected to have already clipped x/y/w/h to the panel's own bounds.
static void set_window(int x, int y, int w, int h)
{
    int x1 = x + w - 1, y1 = y + h - 1;
    uint8_t col[4] = {
        (uint8_t)((kColOffset + x) >> 8), (uint8_t)((kColOffset + x) & 0xFF),
        (uint8_t)((kColOffset + x1) >> 8), (uint8_t)((kColOffset + x1) & 0xFF),
    };
    uint8_t row[4] = {
        (uint8_t)((kRowOffset + y) >> 8), (uint8_t)((kRowOffset + y) & 0xFF),
        (uint8_t)((kRowOffset + y1) >> 8), (uint8_t)((kRowOffset + y1) & 0xFF),
    };
    write_cmd(ST7789_CMD_CASET);
    write_data(col, sizeof(col));
    write_cmd(ST7789_CMD_RASET);
    write_data(row, sizeof(row));
}

#if ST7789_PANEL_HEIGHT > ST7789_HEIGHT
// One-time, at init: the whole glass to black, margins included - the
// framebuffer never covers the margins, and GRAM isn't guaranteed cleared
// by reset. The window starts above framebuffer row 0 by the top margin.
static void clear_glass(void)
{
    set_window(0, -(ST7789_PANEL_HEIGHT - ST7789_HEIGHT) / 2, ST7789_WIDTH, ST7789_PANEL_HEIGHT);
    write_cmd(ST7789_CMD_RAMWR);
    static const uint8_t zeros[ST7789_WIDTH * 2] = {0};
    cs_select();
    gpio_put(s_dc_pin, 1);
    for (int row = 0; row < ST7789_PANEL_HEIGHT; row++) spi_write_blocking(s_spi, zeros, sizeof(zeros));
    cs_deselect();
}
#endif

void st7789_init(spi_inst_t *spi, uint sck_pin, uint mosi_pin, uint cs_pin, uint dc_pin, uint rst_pin, uint bl_pin)
{
    s_spi = spi;
    s_cs_pin = cs_pin;
    s_dc_pin = dc_pin;
    s_bl_pin = bl_pin;

    spi_init(s_spi, ST7789_SPI_HZ);
    gpio_set_function(sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(mosi_pin, GPIO_FUNC_SPI);
    // MISO deliberately not configured - this panel's SDO isn't wired (see
    // PINOUT.md), and nothing here ever reads from the bus.

    // CS is managed by hand (plain GPIO, not GPIO_FUNC_SPI's automatic
    // hardware chip-select) so every command/data write below can bracket
    // its own burst exactly - same convention most ST7789 driver code
    // uses, and it sidesteps any question of how the SPI block's own
    // auto-CS timing lines up with DC.
    gpio_init(s_cs_pin);
    gpio_set_dir(s_cs_pin, GPIO_OUT);
    gpio_put(s_cs_pin, 1);

    gpio_init(s_dc_pin);
    gpio_set_dir(s_dc_pin, GPIO_OUT);

    gpio_init(rst_pin);
    gpio_set_dir(rst_pin, GPIO_OUT);

    // PWM, not a plain GPIO_OUT: st7789_set_backlight_level() needs a duty
    // cycle to vary, and this is the same pin either way (every RP2350 GPIO
    // is PWM-capable), so there's no reason to keep the old on/off-only
    // path around as well.
    gpio_set_function(s_bl_pin, GPIO_FUNC_PWM);
    uint bl_slice = pwm_gpio_to_slice_num(s_bl_pin);
    pwm_set_wrap(bl_slice, BACKLIGHT_PWM_WRAP);
    pwm_set_enabled(bl_slice, true);
    st7789_set_backlight_level(0); // stay dark until the init sequence below finishes - no point lighting up garbage

    // Hardware reset: the datasheet's minimum low pulse is 10us; held for
    // 10ms here, generously, since there's no reason to cut it close on a
    // one-time boot-time reset.
    gpio_put(rst_pin, 1);
    sleep_ms(10);
    gpio_put(rst_pin, 0);
    sleep_ms(10);
    gpio_put(rst_pin, 1);
    sleep_ms(120); // panel's own post-reset settling time before it will accept commands

    write_cmd(ST7789_CMD_SWRESET);
    sleep_ms(150);

    write_cmd(ST7789_CMD_SLPOUT);
    sleep_ms(120);

    write_cmd(ST7789_CMD_COLMOD);
    write_data8(0x55); // 16 bits/pixel (5-6-5) - matches this driver's RGB565 framebuffer

    // MV=0 (no row/column swap); MY (0x80) per kMirrorY, MX (0x40) per
    // kMirrorX, BGR (0x08) per kBgrPanel - see their own comments.
    write_cmd(ST7789_CMD_MADCTL);
    write_data8((kMirrorY ? 0x80 : 0x00) | (kMirrorX ? 0x40 : 0x00) | (kBgrPanel ? 0x08 : 0x00));

    write_cmd(kInvertColors ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
    write_cmd(ST7789_CMD_NORON);
#if ST7789_PANEL_HEIGHT > ST7789_HEIGHT
    clear_glass(); // before DISPON, so the margins never show GRAM leftovers
#endif

    write_cmd(ST7789_CMD_DISPON);
    sleep_ms(100);

    st7789_set_backlight_level(kDefaultBacklightPercent);
}

void st7789_set_backlight_level(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint slice = pwm_gpio_to_slice_num(s_bl_pin);
    uint chan = pwm_gpio_to_channel(s_bl_pin);
    pwm_set_chan_level(slice, chan, (uint32_t)percent * BACKLIGHT_PWM_WRAP / 100);
}

void st7789_set_backlight(bool on)
{
    st7789_set_backlight_level(on ? 100 : 0);
}

void st7789_show(void)
{
    set_window(0, 0, ST7789_WIDTH, ST7789_HEIGHT);
    write_cmd(ST7789_CMD_RAMWR);

    // One long burst: the whole framebuffer, byte-swapped to big-endian on
    // the wire (RGB565 as a 16-bit value is MSB-first per the ST7789
    // datasheet, but s_buf's own uint16_t entries are stored in this
    // machine's native (little) endianness) - spi_write_blocking() takes a
    // byte buffer, so this can't just point it at s_buf directly.
    cs_select();
    gpio_put(s_dc_pin, 1);
    static uint8_t line[ST7789_WIDTH * 2]; // one row at a time - 480 bytes, not 134KB, on the stack/static either way
    for (int row = 0; row < ST7789_HEIGHT; row++) {
        for (int col = 0; col < ST7789_WIDTH; col++) {
            uint16_t px = s_buf[row * ST7789_WIDTH + col];
            line[col * 2] = (uint8_t)(px >> 8);
            line[col * 2 + 1] = (uint8_t)(px & 0xFF);
        }
        spi_write_blocking(s_spi, line, sizeof(line));
    }
    cs_deselect();
}

void st7789_show_rect(int x, int y, int w, int h)
{
    // Same clipping shape as st7789_fill_rect().
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7789_WIDTH) w = ST7789_WIDTH - x;
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    set_window(x, y, w, h);
    write_cmd(ST7789_CMD_RAMWR);

    // Same byte-swap-per-row approach as st7789_show(), just w pixels wide
    // and only for rows y..y+h-1 - see that function's own comment on why
    // the swap can't just point spi_write_blocking() at s_buf directly.
    cs_select();
    gpio_put(s_dc_pin, 1);
    static uint8_t line[ST7789_WIDTH * 2]; // sized for the widest possible call; only the first w*2 bytes are used
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            uint16_t px = s_buf[row * ST7789_WIDTH + col];
            int i = (col - x) * 2;
            line[i] = (uint8_t)(px >> 8);
            line[i + 1] = (uint8_t)(px & 0xFF);
        }
        spi_write_blocking(s_spi, line, (size_t)w * 2);
    }
    cs_deselect();
}
