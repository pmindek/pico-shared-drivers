#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

// ILI9341 SPI TFT driver: a 16-bit (RGB565) framebuffer, pushed to the
// panel over hardware SPI. Written for the 2.8" 240x320 module (no touch,
// pins GND VCC CLK MOSI RES DC BLK MISO - no CS pin, see ili9341_init())
// birch added as an alternative to its 1.69" ST7789 in 2026-10.
//
// Deliberately the same shape as ../st7789 (same function set, same
// framebuffer-then-show model, same PWM backlight) so a project can switch
// between the two at build time without touching anything above its own
// init call - but leaner: no shapes or text here, since every consumer
// draws through ../pigs (pigs_ili9341.h) rather than against the driver
// directly.
//
// Same as st7789: SPI has no address ACK, so there's no presence
// detection, and MISO is never configured or read (the module has the pin,
// but nothing here needs a read-back) - the only way to know the panel is
// there is to look at it.
//
// ---- Framebuffer size vs. panel size ----
// The glass is always ILI9341_PANEL_WIDTH x ILI9341_PANEL_HEIGHT (240x320
// portrait). The framebuffer is ILI9341_WIDTH x ILI9341_HEIGHT, which
// defaults to the full panel but can be made smaller by defining either
// macro on the compiler command line (for *every* source that includes
// this header - e.g. CMake target_compile_definitions(... ILI9341_HEIGHT=280)).
// A smaller framebuffer is shown centered on the glass with black margins
// - how birch runs its 240x280 layouts (made for the ST7789) unchanged on
// this panel: 20 black rows above and below, same picture otherwise.
//
// ---- Landscape ----
// ILI9341_SWAP_XY=1 has the controller swap rows and columns (MADCTL MV),
// so the glass is addressed as 320x240 (ILI9341_PANEL_WIDTH/HEIGHT swap
// too) and the framebuffer defaults to that. Which way round it ends up,
// and whether mirrored, is ILI9341_MIRROR_X/Y (ili9341.c).

#ifndef ILI9341_SWAP_XY
#define ILI9341_SWAP_XY 0
#endif

#if ILI9341_SWAP_XY
#define ILI9341_PANEL_WIDTH 320
#define ILI9341_PANEL_HEIGHT 240
#else
#define ILI9341_PANEL_WIDTH 240
#define ILI9341_PANEL_HEIGHT 320
#endif

#ifndef ILI9341_WIDTH
#define ILI9341_WIDTH ILI9341_PANEL_WIDTH
#endif
#ifndef ILI9341_HEIGHT
#define ILI9341_HEIGHT ILI9341_PANEL_HEIGHT
#endif

#if ILI9341_WIDTH > ILI9341_PANEL_WIDTH || ILI9341_HEIGHT > ILI9341_PANEL_HEIGHT
#error "ILI9341_WIDTH/HEIGHT can't be larger than the panel (240x320, or 320x240 with ILI9341_SWAP_XY)"
#endif

// RGB565, same packing as ST7789_RGB565()/PIGS_RGB(). Always RGB order
// regardless of the panel's internal wiring (kBgrPanel in ili9341.c
// corrects for that at the command level).
#define ILI9341_RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))
#define ILI9341_BLACK ILI9341_RGB565(0, 0, 0)
#define ILI9341_WHITE ILI9341_RGB565(255, 255, 255)

// Pass as cs_pin for a module with no CS pin (CS tied low on the module's
// own PCB, so the controller is always selected).
#define ILI9341_NO_CS (-1)

// Claims spi (calls spi_init() itself - do not init it yourself first) and
// the GPIOs, drives a hardware reset pulse, sends the panel's init
// sequence and clears the whole glass (margins included) to black.
// sck_pin/mosi_pin must be a valid SPI0/SPI1 alternate-function pair for
// `spi`; cs_pin (or ILI9341_NO_CS), dc_pin and rst_pin are plain GPIOs;
// bl_pin is claimed as PWM, see ili9341_set_backlight_level().
//
// Without a CS pin the controller can't use a CS edge to realign on a byte
// boundary - it only counts clock edges since its last reset. So the SPI
// pins are set up (clock idling at its final level) *before* the reset
// pulse, which restarts that count from a clean state. Call once, before
// anything else here.
void ili9341_init(spi_inst_t *spi, uint sck_pin, uint mosi_pin, int cs_pin, uint dc_pin, uint rst_pin, uint bl_pin);

// 0 (off) to 100 (full), via PWM - above 100 clamps to 100. Doesn't touch
// the framebuffer or the panel's contents.
void ili9341_set_backlight_level(uint8_t percent);
void ili9341_set_backlight(bool on);

// Framebuffer drawing - nothing reaches the panel until ili9341_show() /
// ili9341_show_rect(). Coordinates outside 0..ILI9341_WIDTH-1 /
// 0..ILI9341_HEIGHT-1 are clipped.
void ili9341_clear(uint16_t color);
void ili9341_draw_pixel(int x, int y, uint16_t color);
void ili9341_fill_rect(int x, int y, int w, int h, uint16_t color);

// Reads back one framebuffer pixel (not the panel's GRAM) - black outside
// the framebuffer.
uint16_t ili9341_get_pixel(int x, int y);

// Pushes the whole framebuffer to the panel.
void ili9341_show(void);

// Pushes only this rectangle (framebuffer coordinates, clipped) - same
// purpose as st7789_show_rect(): a small changing region without paying
// for a full-frame transfer.
void ili9341_show_rect(int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif
