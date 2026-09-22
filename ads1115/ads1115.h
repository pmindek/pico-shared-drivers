#pragma once

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ADS1115: 4-channel 16-bit ADC over i2c. Here to replace the RP2040/RP2350's
// own ADC for analog controls - joystick axes, pots, pedals - where the
// on-chip converter's noise is the limiting factor rather than its
// resolution. 12 bits is already more than a joystick needs; what this chip
// buys is a far quieter reading, which is what lets a small dead zone and a
// non-linear response curve behave.
//
// This driver does not call i2c_init() and does not set up SDA/SCL's GPIO
// function or pull-ups - the caller owns the bus, as in mpr121.h/aw9523.h.
//
// NON-BLOCKING BY DESIGN. The chip has one converter shared by four inputs,
// so reading all four means four conversions with a mux change between each.
// At the fastest rate that is about 1.2ms per channel: nearly 5ms of
// blocking per scan if done the obvious way, on a loop that also has a
// keyboard to poll. Instead ads1115_update() starts one conversion, returns
// immediately, and collects the result on a later call - so a full scan
// costs a handful of short i2c transactions spread over ~6ms, and nothing
// waits.
//
// NOT RATIOMETRIC, unlike the Pico's own ADC. This chip measures against an
// internal reference, so ripple on the supply feeding a potentiometer turns
// into apparent movement. It doesn't matter for a joystick with a dead zone;
// it does for anything trying to be accurate. Give the pots a clean supply.
//
// Inputs must not exceed the chip's own supply - so run both from the same
// 3.3V rail, never a 5V pot into a 3.3V chip.

#define ADS1115_CHANNEL_COUNT 4

// ADDR to GND. The other straps give 0x49 (VDD), 0x4A (SDA) and 0x4B (SCL).
#define ADS1115_DEFAULT_ADDR 0x48

// Full-scale input range. ADS1115_PGA_4_096V is the one to use on a 3.3V
// rail: the next range down (2.048V) would clip a 3.3V signal, and the range
// above (6.144V) throws away half the resolution for headroom that can't
// exist, since the input can never exceed the supply anyway.
typedef enum {
    ADS1115_PGA_6_144V = 0,
    ADS1115_PGA_4_096V = 1,
    ADS1115_PGA_2_048V = 2,
    ADS1115_PGA_1_024V = 3,
    ADS1115_PGA_0_512V = 4,
    ADS1115_PGA_0_256V = 5,
} ads1115_pga_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    ads1115_pga_t pga;
    uint8_t channel_mask; // which of the 4 inputs to scan, bit per channel

    int current;              // channel whose conversion is in flight, -1 if none
    uint64_t ready_at_us;     // when that conversion should be done
    int16_t raw[ADS1115_CHANNEL_COUNT];
    bool valid[ADS1115_CHANNEL_COUNT]; // false until that channel has been read once
    uint32_t errors;                   // failed transactions since init, for diagnostics
} ads1115_t;

// Probes the chip and prepares a scan of every channel in channel_mask (bit
// 0 = AIN0). Pass 0x0F for all four.
//
// Returns false unless the chip both answers and behaves like an ADS1115:
// the threshold register is written and read back, since the ADS1115 has no
// ID register and an address ACK alone proves very little. Same intent as
// the MPR121 and AW9523 identity checks.
bool ads1115_init(ads1115_t *dev, i2c_inst_t *i2c, uint8_t addr, uint8_t channel_mask, ads1115_pga_t pga);

// Advances the scan: collects a finished conversion and/or starts the next
// channel's. Call once per main-loop iteration; calling it more often is
// harmless and simply finds nothing to do. Never blocks waiting for a
// conversion.
//
// Returns false only if an i2c transaction actually failed - which lets a
// caller feed a run of failures into whatever bus-recovery it has, the same
// contract as mpr121_update() and aw9523_update().
bool ads1115_update(ads1115_t *dev);

// The most recent raw reading for a channel: a signed 16-bit count, where
// the PGA range maps to +-32767. Reads 0 for a channel that hasn't been
// converted yet - check ads1115_valid() if that distinction matters.
int16_t ads1115_raw(const ads1115_t *dev, int channel);
bool ads1115_valid(const ads1115_t *dev, int channel);

// The same reading in volts.
float ads1115_volts(const ads1115_t *dev, int channel);

// The same reading rescaled to the 0-4095 range a 12-bit 0-3.3V ADC would
// have produced, clamped. This is the bridge to code written against the
// Pico's own ADC - joystick.c's centre calibration, dead zone and curve all
// keep their tuning instead of needing new constants for a different scale.
uint16_t ads1115_read_12bit(const ads1115_t *dev, int channel);

// How long a full scan of the enabled channels takes, in microseconds -
// useful for deciding how stale a reading might be.
uint32_t ads1115_scan_period_us(const ads1115_t *dev);

#ifdef __cplusplus
}
#endif
