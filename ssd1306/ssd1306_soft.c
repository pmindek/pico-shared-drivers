#include "ssd1306_soft.h"
#include <string.h>
#include <stdlib.h>
#include "hardware/gpio.h"

static uint s_sda, s_scl;
static uint8_t s_addr;
static uint8_t s_buf[SSD1306_SOFT_WIDTH * SSD1306_SOFT_HEIGHT / 8];

// Open-drain bit-bang: "low" drives the pin out low, "high" releases it to float (pull-up,
// internal or on-module, brings it back up). Never drive high directly.
static inline void sda_low(void)  { gpio_set_dir(s_sda, GPIO_OUT); gpio_put(s_sda, 0); }
static inline void sda_high(void) { gpio_set_dir(s_sda, GPIO_IN); }
static inline void scl_low(void)  { gpio_set_dir(s_scl, GPIO_OUT); gpio_put(s_scl, 0); }
static inline void scl_high(void) { gpio_set_dir(s_scl, GPIO_IN); }

static inline void i2c_delay(void) { sleep_us(3); } // ~100kHz-ish, SSD1306 doesn't need more

static void i2c_start(void) {
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();  i2c_delay();
}

static void i2c_stop(void) {
    sda_low();  i2c_delay();
    scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}

// Clocks one byte out MSB-first; returns true if the slave ACKed.
static bool i2c_write_byte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) sda_high(); else sda_low();
        i2c_delay();
        scl_high(); i2c_delay();
        scl_low();
        b <<= 1;
    }

    sda_high(); i2c_delay(); // release SDA so the slave can pull it low for ACK
    scl_high(); i2c_delay();
    bool ack = !gpio_get(s_sda);
    scl_low();

    return ack;
}

static void i2c_write(const uint8_t *data, size_t len) {
    i2c_start();
    i2c_write_byte((uint8_t)(s_addr << 1)); // write direction
    for (size_t i = 0; i < len; i++) {
        i2c_write_byte(data[i]);
    }
    i2c_stop();
}

static void ssd1306_soft_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write(buf, 2);
}

void ssd1306_soft_init(uint sda_pin, uint scl_pin, uint8_t addr) {
    s_sda = sda_pin;
    s_scl = scl_pin;
    s_addr = addr;

    gpio_init(s_sda);
    gpio_init(s_scl);
    gpio_pull_up(s_sda);
    gpio_pull_up(s_scl);
    sda_high();
    scl_high();

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
        ssd1306_soft_cmd(init_cmds[i]);
    }

    ssd1306_soft_clear();
    ssd1306_soft_show();
}

void ssd1306_soft_clear(void) {
    memset(s_buf, 0, sizeof(s_buf));
}

void ssd1306_soft_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SSD1306_SOFT_WIDTH || y < 0 || y >= SSD1306_SOFT_HEIGHT) return;
    size_t idx = x + (y / 8) * SSD1306_SOFT_WIDTH;
    uint8_t mask = 1 << (y % 8);
    if (on) {
        s_buf[idx] |= mask;
    } else {
        s_buf[idx] &= ~mask;
    }
}

void ssd1306_soft_draw_line(int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        ssd1306_soft_draw_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ssd1306_soft_show(void) {
    ssd1306_soft_cmd(0x21); // set column address
    ssd1306_soft_cmd(0);
    ssd1306_soft_cmd(SSD1306_SOFT_WIDTH - 1);
    ssd1306_soft_cmd(0x22); // set page address
    ssd1306_soft_cmd(0);
    ssd1306_soft_cmd((SSD1306_SOFT_HEIGHT / 8) - 1);

    uint8_t tx[SSD1306_SOFT_WIDTH + 1];
    tx[0] = 0x40; // control byte: data
    for (int page = 0; page < SSD1306_SOFT_HEIGHT / 8; page++) {
        memcpy(&tx[1], &s_buf[page * SSD1306_SOFT_WIDTH], SSD1306_SOFT_WIDTH);
        i2c_write(tx, SSD1306_SOFT_WIDTH + 1);
    }
}
