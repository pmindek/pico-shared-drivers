#pragma once

// SSD1306 driver over bit-banged (software) I2C. Independent of the hardware I2C0/I2C1
// peripherals — used for a second display sharing no hardware bus with anything else.

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#define SSD1306_SOFT_WIDTH  128
#define SSD1306_SOFT_HEIGHT 64

#ifdef __cplusplus
extern "C" {
#endif

void ssd1306_soft_init(uint sda_pin, uint scl_pin, uint8_t addr);

void ssd1306_soft_clear(void);
void ssd1306_soft_draw_pixel(int x, int y, bool on);
void ssd1306_soft_draw_line(int x0, int y0, int x1, int y1, bool on);

void ssd1306_soft_show(void);

#ifdef __cplusplus
}
#endif
