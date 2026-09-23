#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

// ST7789 SPI TFT driver: a 16-bit (RGB565) framebuffer, pushed to the panel
// over hardware SPI. Sized for the 240x280 1.69" module in gyros' PINOUT.md
// Plan B, but nothing here is specific to that panel beyond the two
// ST7789_WIDTH/HEIGHT constants - other ST7789 sizes would just need those
// changed (and possibly the offset/BGR/inversion knobs in st7789.c - see
// their own comments).
//
// Unlike this library's i2c drivers (mpr121.h, ads1115.h, aw9523.h), there
// is no presence detection here. i2c has an address ACK; SPI doesn't - the
// master just shifts bits out on a clock it fully controls, whether or not
// anything is listening, so there's no hang risk the way i2c_write_
// blocking() has either (nothing here needs a _timeout_us equivalent).
// This panel's MISO isn't wired (see PINOUT.md - nothing here ever reads
// from it), so there's no read-ID command to confirm it's alive either.
// The only way to know it's there is to look at it.
//
// The framebuffer is internal and static: 240*280*2 = 134,400 bytes, RGB565,
// one uint16_t per pixel, row-major. Comfortably affordable on this board
// (gyros' whole build uses well under that in bss already) - simpler than
// windowed/partial pushes, and there was no memory pressure forcing
// anything cleverer. Nothing is sent to the panel until st7789_show().

#define ST7789_WIDTH 240
#define ST7789_HEIGHT 280

// RGB565: 5 bits red, 6 green, 5 blue, packed MSB-first. These are defined
// in RGB order regardless of how the physical panel is wired (see
// kBgrPanel in st7789.c) - the driver corrects for BGR panels at the
// command level (MADCTL), so a color named here always means the same
// thing on screen.
#define ST7789_RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))
#define ST7789_BLACK ST7789_RGB565(0, 0, 0)
#define ST7789_WHITE ST7789_RGB565(255, 255, 255)
#define ST7789_RED ST7789_RGB565(255, 0, 0)
#define ST7789_GREEN ST7789_RGB565(0, 255, 0)
#define ST7789_BLUE ST7789_RGB565(0, 0, 255)
#define ST7789_YELLOW ST7789_RGB565(255, 255, 0)
#define ST7789_CYAN ST7789_RGB565(0, 255, 255)
#define ST7789_MAGENTA ST7789_RGB565(255, 0, 255)

// Claims spi (calls spi_init() itself - do not init it yourself first) and
// the 4 GPIOs, drives a hardware reset pulse, and sends the panel's init
// sequence. sck_pin/mosi_pin must be a valid SPI0/SPI1 alternate-function
// pair for whichever `spi` instance you pass (see PINOUT.md's own note on
// how those were chosen for gyros) - cs_pin/dc_pin/rst_pin/bl_pin are
// plain GPIOs, any free pin works. bl_pin is driven as a plain on/off
// output here, not PWM - see st7789_set_backlight()'s own comment if you
// want dimming.
//
// Returns void, not bool - see this header's own comment on why presence
// can't be detected. Call it once, before anything else here.
void st7789_init(spi_inst_t *spi, uint sck_pin, uint mosi_pin, uint cs_pin, uint dc_pin, uint rst_pin, uint bl_pin);

// On/off only - see st7789_init()'s own comment. Independent of
// st7789_show()/the framebuffer, same convention as ssd1306_set_display_
// on(): whatever's already on screen stays there, just unlit or lit.
void st7789_set_backlight(bool on);

// Framebuffer drawing - nothing reaches the panel until st7789_show().
// Coordinates outside 0..ST7789_WIDTH-1 / 0..ST7789_HEIGHT-1 are clipped
// (silently ignored/truncated), same convention as ssd1306's own drawing
// calls.
void st7789_clear(uint16_t color);
void st7789_draw_pixel(int x, int y, uint16_t color);
void st7789_fill_rect(int x, int y, int w, int h, uint16_t color);
void st7789_draw_circle(int cx, int cy, int r, uint16_t color);
void st7789_fill_circle(int cx, int cy, int r, uint16_t color);

// Pushes the whole framebuffer to the panel over SPI - one CASET/RASET
// covering the full screen, then one RAMWR burst of all 134,400 bytes.
// Blocking; see st7789.c's own comment on the clock speed / how long this
// takes.
void st7789_show(void);

#ifdef __cplusplus
}
#endif
