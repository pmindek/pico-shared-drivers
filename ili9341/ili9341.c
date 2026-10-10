#include "ili9341.h"

#include <stddef.h>

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

// ---- ILI9341 command opcodes (ILI9341 datasheet, section 8) ----
#define ILI9341_CMD_SWRESET 0x01
#define ILI9341_CMD_SLPOUT 0x11
#define ILI9341_CMD_NORON 0x13
#define ILI9341_CMD_INVOFF 0x20
#define ILI9341_CMD_INVON 0x21
#define ILI9341_CMD_GAMMASET 0x26
#define ILI9341_CMD_DISPON 0x29
#define ILI9341_CMD_CASET 0x2A
#define ILI9341_CMD_PASET 0x2B
#define ILI9341_CMD_RAMWR 0x2C
#define ILI9341_CMD_MADCTL 0x36
#define ILI9341_CMD_VSCRSADD 0x37
#define ILI9341_CMD_PIXFMT 0x3A
#define ILI9341_CMD_FRMCTR1 0xB1
#define ILI9341_CMD_DFUNCTR 0xB6
#define ILI9341_CMD_PWCTR1 0xC0
#define ILI9341_CMD_PWCTR2 0xC1
#define ILI9341_CMD_VMCTR1 0xC5
#define ILI9341_CMD_VMCTR2 0xC7
#define ILI9341_CMD_GMCTRP1 0xE0
#define ILI9341_CMD_GMCTRN1 0xE1

// MADCTL bits
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_BGR 0x08

// ---- Panel calibration knobs ----
// UNVERIFIED first guesses (2026-10-09, written before the panel was ever
// connected): set to what's most common for 2.8" ILI9341 modules. Each one
// has an obvious visual symptom if wrong - fix by flipping that one
// constant, not by guessing at the others.

// Nearly every ILI9341 module is wired BGR (Adafruit's driver sets the bit
// unconditionally). Symptom if wrong: red and blue swapped.
static const bool kBgrPanel = true;

// ILI9341 modules are usually TN glass, which wants inversion OFF; some
// newer "IPS" ones want it ON. Symptom if wrong: colors look like a photo
// negative (black background shows white).
static const bool kInvertColors = false;

// Which way up the image sits relative to the pin header. MX set alone is
// Adafruit's "rotation 0" portrait. Symptom if wrong: picture mirrored
// left-right (flip kMirrorX) or upside-down-mirrored (flip kMirrorY);
// upside down but not mirrored = flip both. Overridable per build
// (-DILI9341_MIRROR_X=0 etc.). With ILI9341_SWAP_XY (landscape, ili9341.h)
// the default is MV alone (Adafruit's "rotation 1"); its 180-degree turn
// is MV+MX+MY. MV+MX or MV+MY alone would be mirrored.
#ifndef ILI9341_MIRROR_X
#define ILI9341_MIRROR_X (!ILI9341_SWAP_XY) // MX for portrait, MV alone for landscape - see above
#endif
#ifndef ILI9341_MIRROR_Y
#define ILI9341_MIRROR_Y 0
#endif
static const bool kMirrorX = ILI9341_MIRROR_X;
static const bool kMirrorY = ILI9341_MIRROR_Y;
static const bool kSwapXY = ILI9341_SWAP_XY;

// SPI mode. ILI9341 samples MOSI on the rising clock edge, which modes 0
// and 3 both do; mode 0 is what most ILI9341 libraries use. Without a CS
// pin, though, some panels (more often reported for CS-less ST7789
// modules) only lock on in mode 3 - if the panel stays blank/white with
// the backlight on, this is the first thing to try (inferred from
// community reports, not from the datasheet).
static const bool kSpiMode3 = false;

// 20MHz, same as st7789.c on the same breadboard wiring. The datasheet's
// write cycle (100ns min) works out to 10MHz officially, but ILI9341s are
// routinely run at 40MHz+; if the picture shows noise/shifted pixels,
// halve this before anything else.
#define ILI9341_SPI_HZ (20 * 1000 * 1000)

#define BACKLIGHT_PWM_WRAP 1499 // same as st7789.c: ~100kHz at 150MHz - see there for why not lower
static const uint8_t kDefaultBacklightPercent = 20;

// Where the framebuffer sits on the glass - centered (see ili9341.h).
#define ORIGIN_X ((ILI9341_PANEL_WIDTH - ILI9341_WIDTH) / 2)
#define ORIGIN_Y ((ILI9341_PANEL_HEIGHT - ILI9341_HEIGHT) / 2)

static uint16_t s_buf[ILI9341_WIDTH * ILI9341_HEIGHT]; // row-major RGB565

static spi_inst_t *s_spi;
static int s_cs_pin;
static uint s_dc_pin, s_bl_pin;

// ---- Framebuffer drawing - no hardware I/O ----

void ili9341_clear(uint16_t color)
{
    for (int i = 0; i < ILI9341_WIDTH * ILI9341_HEIGHT; i++) s_buf[i] = color;
}

void ili9341_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= ILI9341_WIDTH || y < 0 || y >= ILI9341_HEIGHT) return;
    s_buf[y * ILI9341_WIDTH + x] = color;
}

uint16_t ili9341_get_pixel(int x, int y)
{
    if (x < 0 || x >= ILI9341_WIDTH || y < 0 || y >= ILI9341_HEIGHT) return 0; // black
    return s_buf[y * ILI9341_WIDTH + x];
}

void ili9341_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > ILI9341_WIDTH) x1 = ILI9341_WIDTH;
    if (y1 > ILI9341_HEIGHT) y1 = ILI9341_HEIGHT;

    for (int j = y0; j < y1; j++) {
        for (int i = x0; i < x1; i++) {
            s_buf[j * ILI9341_WIDTH + i] = color;
        }
    }
}

// ---- SPI ----

static void cs_select(void)
{
    if (s_cs_pin >= 0) gpio_put((uint)s_cs_pin, 0);
}

static void cs_deselect(void)
{
    if (s_cs_pin >= 0) gpio_put((uint)s_cs_pin, 1);
}

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

static void write_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    write_cmd(cmd);
    if (len) write_data(data, len);
}

// Sets the controller's active window to a rectangle in *panel*
// coordinates (callers add ORIGIN_X/ORIGIN_Y themselves).
static void set_panel_window(int x, int y, int w, int h)
{
    int x1 = x + w - 1, y1 = y + h - 1;
    uint8_t col[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t row[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
    write_cmd_data(ILI9341_CMD_CASET, col, sizeof(col));
    write_cmd_data(ILI9341_CMD_PASET, row, sizeof(row));
}

// One-time: the whole glass to black, margins included - the framebuffer
// never covers the margins, and GRAM isn't guaranteed cleared by reset.
static void clear_panel(void)
{
    set_panel_window(0, 0, ILI9341_PANEL_WIDTH, ILI9341_PANEL_HEIGHT);
    write_cmd(ILI9341_CMD_RAMWR);
    static const uint8_t zeros[ILI9341_PANEL_WIDTH * 2] = {0};
    cs_select();
    gpio_put(s_dc_pin, 1);
    for (int row = 0; row < ILI9341_PANEL_HEIGHT; row++) spi_write_blocking(s_spi, zeros, sizeof(zeros));
    cs_deselect();
}

void ili9341_init(spi_inst_t *spi, uint sck_pin, uint mosi_pin, int cs_pin, uint dc_pin, uint rst_pin, uint bl_pin)
{
    s_spi = spi;
    s_cs_pin = cs_pin;
    s_dc_pin = dc_pin;
    s_bl_pin = bl_pin;

    spi_init(s_spi, ILI9341_SPI_HZ);
    if (kSpiMode3) spi_set_format(s_spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(mosi_pin, GPIO_FUNC_SPI);
    // MISO deliberately not configured - nothing here reads from the panel.

    // CS (if any) by hand, same reasoning as st7789.c.
    if (s_cs_pin >= 0) {
        gpio_init((uint)s_cs_pin);
        gpio_set_dir((uint)s_cs_pin, GPIO_OUT);
        gpio_put((uint)s_cs_pin, 1);
    }

    gpio_init(s_dc_pin);
    gpio_set_dir(s_dc_pin, GPIO_OUT);

    gpio_init(rst_pin);
    gpio_set_dir(rst_pin, GPIO_OUT);

    gpio_set_function(s_bl_pin, GPIO_FUNC_PWM);
    uint bl_slice = pwm_gpio_to_slice_num(s_bl_pin);
    pwm_set_wrap(bl_slice, BACKLIGHT_PWM_WRAP);
    pwm_set_enabled(bl_slice, true);
    ili9341_set_backlight_level(0); // dark until the init sequence below is done

    // Hardware reset - after the SPI pins are set up, see ili9341.h on why
    // that order matters for a CS-less module. Datasheet minimum low pulse
    // is 10us, then 120ms before sleep-out is allowed.
    gpio_put(rst_pin, 1);
    sleep_ms(10);
    gpio_put(rst_pin, 0);
    sleep_ms(10);
    gpio_put(rst_pin, 1);
    sleep_ms(120);

    write_cmd(ILI9341_CMD_SWRESET);
    sleep_ms(150);

    // Power/VCOM/gamma values as used by Adafruit_ILI9341 (the de-facto
    // reference most of these modules are tested against) - recalled from
    // that library, not re-derived from the datasheet. The vendor
    // extended-register writes (0xCF/0xED/0xE8/0xCB/0xF7/0xEA) are the same
    // sequence that library sends.
    static const uint8_t kInit[] = {
        // cmd, n, data...
        0xEF, 3, 0x03, 0x80, 0x02,
        0xCF, 3, 0x00, 0xC1, 0x30,
        0xED, 4, 0x64, 0x03, 0x12, 0x81,
        0xE8, 3, 0x85, 0x00, 0x78,
        0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
        0xF7, 1, 0x20,
        0xEA, 2, 0x00, 0x00,
        ILI9341_CMD_PWCTR1, 1, 0x23,
        ILI9341_CMD_PWCTR2, 1, 0x10,
        ILI9341_CMD_VMCTR1, 2, 0x3E, 0x28,
        ILI9341_CMD_VMCTR2, 1, 0x86,
        ILI9341_CMD_VSCRSADD, 1, 0x00,
        ILI9341_CMD_PIXFMT, 1, 0x55, // 16 bits/pixel, matches the RGB565 framebuffer
        ILI9341_CMD_FRMCTR1, 2, 0x00, 0x18,
        ILI9341_CMD_DFUNCTR, 3, 0x08, 0x82, 0x27,
        0xF2, 1, 0x00, // 3-gamma off
        ILI9341_CMD_GAMMASET, 1, 0x01,
        ILI9341_CMD_GMCTRP1, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
        ILI9341_CMD_GMCTRN1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    };
    for (size_t i = 0; i < sizeof(kInit);) {
        uint8_t cmd = kInit[i++];
        uint8_t n = kInit[i++];
        write_cmd_data(cmd, &kInit[i], n);
        i += n;
    }

    uint8_t madctl = (kMirrorX ? MADCTL_MX : 0) | (kMirrorY ? MADCTL_MY : 0) | (kSwapXY ? MADCTL_MV : 0) |
                     (kBgrPanel ? MADCTL_BGR : 0);
    write_cmd_data(ILI9341_CMD_MADCTL, &madctl, 1);
    write_cmd(kInvertColors ? ILI9341_CMD_INVON : ILI9341_CMD_INVOFF);

    write_cmd(ILI9341_CMD_SLPOUT);
    sleep_ms(120);
    write_cmd(ILI9341_CMD_NORON);
    clear_panel(); // before DISPON, so the panel never shows GRAM leftovers
    write_cmd(ILI9341_CMD_DISPON);
    sleep_ms(100);

    ili9341_set_backlight_level(kDefaultBacklightPercent);
}

void ili9341_set_backlight_level(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint slice = pwm_gpio_to_slice_num(s_bl_pin);
    uint chan = pwm_gpio_to_channel(s_bl_pin);
    pwm_set_chan_level(slice, chan, (uint32_t)percent * (BACKLIGHT_PWM_WRAP + 1) / 100); // 100% = level past the wrap = steady high, no switching
}

void ili9341_set_backlight(bool on)
{
    ili9341_set_backlight_level(on ? 100 : 0);
}

void ili9341_show(void)
{
    ili9341_show_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT);
}

void ili9341_show_rect(int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ILI9341_WIDTH) w = ILI9341_WIDTH - x;
    if (y + h > ILI9341_HEIGHT) h = ILI9341_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    set_panel_window(ORIGIN_X + x, ORIGIN_Y + y, w, h);
    write_cmd(ILI9341_CMD_RAMWR);

    // Row by row, byte-swapped to big-endian on the wire (the controller
    // takes RGB565 MSB-first; s_buf is little-endian) - same as st7789.c.
    cs_select();
    gpio_put(s_dc_pin, 1);
    static uint8_t line[ILI9341_WIDTH * 2];
    for (int row = y; row < y + h; row++) {
        const uint16_t *src = &s_buf[row * ILI9341_WIDTH + x];
        for (int col = 0; col < w; col++) {
            line[col * 2] = (uint8_t)(src[col] >> 8);
            line[col * 2 + 1] = (uint8_t)src[col];
        }
        spi_write_blocking(s_spi, line, (size_t)w * 2);
    }
    cs_deselect();
}
