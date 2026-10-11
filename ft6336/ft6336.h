#pragma once

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// FT6336U capacitive touch controller (FocalTech) over i2c - the touch
// layer of the 240x320 IPS ST7789 modules with a CTP. Reads the first touch
// point only; the chip tracks two, nothing here needs the second.
//
// This driver does not call i2c_init() and does not set up SDA/SCL's GPIO
// function or pull-ups - the caller owns the bus, as in ads1115.h. Every
// transaction has a timeout, so a chip that stops answering can't hang the
// caller.
//
// Reset and power-up are split in two so a caller can overlap the chip's
// start-up time with its own: ft6336_setup() pulses RST, ft6336_probe()
// (any time later) waits out whatever is left of FT6336_POWER_ON_MS and
// then talks to the chip.
//
// Use it through ../touch (touch_ft6336.h) to get screen coordinates and
// taps; this layer only knows the chip's own coordinates.

#define FT6336_DEFAULT_ADDR 0x38

// From reset released until the chip answers and reports points. 300 ms is
// the figure FocalTech's FT6x36 timing tables and the common drivers use -
// taken from there, not measured on this chip.
#define FT6336_POWER_ON_MS 300

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    int rst_pin; // -1 = not wired (the module's own power-on reset then)
    int int_pin; // -1 = not wired (ft6336_int_asserted() then always says true)
    uint64_t reset_released_us;
    bool present;
    uint8_t chip_id, vendor_id, firmware_version; // what ft6336_probe() read, for the boot log
    uint32_t errors; // failed transactions since probe, for diagnostics
} ft6336_t;

// Stores the wiring, sets INT up as an input with a pull-up, and pulses RST
// low (a few ms, blocking) - no i2c traffic, so the bus doesn't need to be
// up yet.
void ft6336_setup(ft6336_t *dev, i2c_inst_t *i2c, uint8_t addr, int rst_pin, int int_pin);

// Waits until FT6336_POWER_ON_MS after ft6336_setup()'s reset (sleeps only
// for what's left), then checks that the chip answers, reads its ids and
// switches INT to "low while touched". Returns (and stores in
// dev->present) whether it answered.
bool ft6336_probe(ft6336_t *dev);

// The first touch point, in the chip's own coordinates. Returns true if a
// finger is down; false if not, or if the read failed (counted in
// dev->errors).
bool ft6336_read(ft6336_t *dev, int *x, int *y);

// True while the INT line is low - after ft6336_probe(), the chip holds it
// low for as long as something touches the panel. A caller can skip the
// bus entirely while it's high and nothing was touching.
bool ft6336_int_asserted(const ft6336_t *dev);

// Puts the chip into hibernate - its lowest-power mode, no scanning, no
// reports - for a caller that switches touch off. Only a reset brings it
// back (FocalTech's application note - not measured or tried otherwise on
// this chip): ft6336_setup() again, then ft6336_probe() once
// FT6336_POWER_ON_MS has passed. Returns whether the chip took the command.
bool ft6336_hibernate(ft6336_t *dev);

// True once FT6336_POWER_ON_MS have passed since ft6336_setup()'s reset -
// ft6336_probe() then answers without waiting.
bool ft6336_powered_up(const ft6336_t *dev);

#ifdef __cplusplus
}
#endif
