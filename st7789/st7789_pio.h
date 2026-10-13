#pragma once

#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Optional add-on to st7789.c: pushes RGB565 pixels to the panel as 12-bit
// (RGB444) through a PIO state machine instead of the SPI block - 25% less
// data on the wire, so a frame goes out in 3/4 of the time at the same bit
// rate. The PIO drops each pixel's lowest bits as it shifts it out (red's
// 1, green's 2, blue's 1), straight from the framebuffer by DMA - no CPU
// pass, no second buffer. First user: birch's FLIGHT (2026-10-13).
//
// The panel must be in its 12-bit format meanwhile - st7789_set_12bit_pixels(
// true) - and back in 16 bits (false) before any of st7789.c's own pushes.
// During a push SCK and MOSI are handed to the PIO; afterwards (in
// st7789_pio_push_wait()) back to the SPI block. Needs hardware_pio linked.
// Colors: 4096 instead of 65536 - smooth gradients (the sky) band visibly.

// Loads the program into `pio` and claims a state machine and a DMA
// channel. bit_hz: the bit clock (sys clock / 2 / a divider - e.g. 37.5
// MHz at 150 MHz). False if the PIO has no room.
bool st7789_pio_init(PIO pio, uint sck_pin, uint mosi_pin, uint32_t bit_hz);

// Starts pushing the w x h pixels (row-major, native RGB565, an even
// count) to x,y - returns at once, the DMA does it. False if not set up or
// out of the panel.
bool st7789_pio_push_async(int x, int y, int w, int h, const uint16_t *pixels);

// Waits for the push (if any) to be over - the last bits out - and gives
// the pins back to the SPI block.
void st7789_pio_push_wait(void);

bool st7789_pio_busy(void);

#ifdef __cplusplus
}
#endif
