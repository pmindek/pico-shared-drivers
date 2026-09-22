#include "ads1115.h"

// Register pointers - see the ADS1115 datasheet, "Register Map".
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 0x01
#define ADS1115_REG_LO_THRESH 0x02
#define ADS1115_REG_HI_THRESH 0x03

// Config register fields (16-bit, big-endian on the wire).
#define ADS1115_CFG_OS_SINGLE 0x8000u    // writing 1 starts a single conversion
#define ADS1115_CFG_MUX_SINGLE 0x4000u   // + channel << 12 = single-ended AINn vs GND
#define ADS1115_CFG_MODE_SINGLE 0x0100u  // single-shot rather than continuous
#define ADS1115_CFG_DR_860SPS 0x00E0u    // fastest rate: ~1.16ms per conversion
#define ADS1115_CFG_COMP_DISABLE 0x0003u // comparator off, ALERT/RDY left alone

// Default threshold-register values after a power-on reset.
#define ADS1115_LO_THRESH_DEFAULT 0x8000
#define ADS1115_HI_THRESH_DEFAULT 0x7FFF

// A conversion at 860SPS takes ~1.16ms. The margin covers the chip's own
// tolerance on its internal oscillator - polling the config register's
// ready bit instead would cost an extra i2c transaction per channel to save
// a few hundred microseconds that nothing here needs.
#define ADS1115_CONVERSION_US 1500

// No timeout on this SDK's blocking i2c calls - a chip that never ACKs hangs
// them forever. Same reasoning and value as mpr121.c/aw9523.c.
#define ADS1115_I2C_TIMEOUT_US 50000

// Counts per volt, per PGA range. At +-4.096V full scale the converter
// produces exactly 8000 counts per volt, which is where the 12-bit
// conversion below gets its numbers.
static const float kVoltsPerCount[] = {
    6.144f / 32768.0f, 4.096f / 32768.0f, 2.048f / 32768.0f,
    1.024f / 32768.0f, 0.512f / 32768.0f, 0.256f / 32768.0f,
};

static bool write_reg(ads1115_t *dev, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return i2c_write_timeout_us(dev->i2c, dev->addr, buf, 3, false, ADS1115_I2C_TIMEOUT_US) == 3;
}

static bool read_reg(ads1115_t *dev, uint8_t reg, uint16_t *value) {
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, ADS1115_I2C_TIMEOUT_US) != 1) return false;
    uint8_t buf[2];
    if (i2c_read_timeout_us(dev->i2c, dev->addr, buf, 2, false, ADS1115_I2C_TIMEOUT_US) != 2) return false;
    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return true;
}

static bool valid_channel(int channel) {
    return channel >= 0 && channel < ADS1115_CHANNEL_COUNT;
}

// The next enabled channel after `from`, wrapping; -1 if none are enabled.
static int next_channel(const ads1115_t *dev, int from) {
    for (int i = 1; i <= ADS1115_CHANNEL_COUNT; i++) {
        int candidate = (from + i) % ADS1115_CHANNEL_COUNT;
        if (dev->channel_mask & (1u << candidate)) return candidate;
    }
    return -1;
}

static bool start_conversion(ads1115_t *dev, int channel) {
    uint16_t config = ADS1115_CFG_OS_SINGLE | ADS1115_CFG_MUX_SINGLE | ((uint16_t)channel << 12) |
                      ((uint16_t)dev->pga << 9) | ADS1115_CFG_MODE_SINGLE | ADS1115_CFG_DR_860SPS |
                      ADS1115_CFG_COMP_DISABLE;
    if (!write_reg(dev, ADS1115_REG_CONFIG, config)) return false;
    dev->current = channel;
    dev->ready_at_us = time_us_64() + ADS1115_CONVERSION_US;
    return true;
}

bool ads1115_init(ads1115_t *dev, i2c_inst_t *i2c, uint8_t addr, uint8_t channel_mask, ads1115_pga_t pga) {
    dev->i2c = i2c;
    dev->addr = addr;
    dev->pga = pga;
    dev->channel_mask = (uint8_t)(channel_mask & 0x0F);
    dev->current = -1;
    dev->ready_at_us = 0;
    dev->errors = 0;
    for (int i = 0; i < ADS1115_CHANNEL_COUNT; i++) {
        dev->raw[i] = 0;
        dev->valid[i] = false;
    }
    if (dev->channel_mask == 0) return false;

    // Identity check. The ADS1115 has no ID register, and an address ACK
    // alone would also come from any other chip strapped to 0x48 - a very
    // crowded address. The threshold registers are plain read/write storage
    // that nothing else here touches, so writing a pattern and reading it
    // back is a real "this behaves like an ADS1115" test rather than a
    // "something is there" one. Restored immediately afterwards so the
    // comparator, if anyone ever enables it, sees its normal defaults.
    const uint16_t kProbePattern = 0x1234;
    if (!write_reg(dev, ADS1115_REG_LO_THRESH, kProbePattern)) return false;
    uint16_t readback = 0;
    if (!read_reg(dev, ADS1115_REG_LO_THRESH, &readback)) return false;
    if (readback != kProbePattern) return false;
    if (!write_reg(dev, ADS1115_REG_LO_THRESH, ADS1115_LO_THRESH_DEFAULT)) return false;
    if (!write_reg(dev, ADS1115_REG_HI_THRESH, ADS1115_HI_THRESH_DEFAULT)) return false;

    // Kick off the first conversion so the scan is already running by the
    // time anyone calls ads1115_update().
    int first = next_channel(dev, ADS1115_CHANNEL_COUNT - 1);
    if (first < 0) return false;
    return start_conversion(dev, first);
}

bool ads1115_update(ads1115_t *dev) {
    if (dev->current < 0) {
        int channel = next_channel(dev, ADS1115_CHANNEL_COUNT - 1);
        if (channel < 0) return true; // nothing enabled - not an error
        if (start_conversion(dev, channel)) return true;
        dev->errors++;
        return false;
    }

    if (time_us_64() < dev->ready_at_us) return true; // still converting - come back later

    uint16_t value = 0;
    if (!read_reg(dev, ADS1115_REG_CONVERSION, &value)) {
        dev->errors++;
        // Drop this channel's conversion and move on rather than retrying
        // the same one forever: on a bus that's misbehaving, a stuck scan
        // would freeze every other axis too.
        dev->current = -1;
        return false;
    }

    dev->raw[dev->current] = (int16_t)value;
    dev->valid[dev->current] = true;

    int next = next_channel(dev, dev->current);
    dev->current = -1;
    if (next < 0) return true;
    if (start_conversion(dev, next)) return true;
    dev->errors++;
    return false;
}

int16_t ads1115_raw(const ads1115_t *dev, int channel) {
    if (!valid_channel(channel)) return 0;
    return dev->raw[channel];
}

bool ads1115_valid(const ads1115_t *dev, int channel) {
    if (!valid_channel(channel)) return false;
    return dev->valid[channel];
}

float ads1115_volts(const ads1115_t *dev, int channel) {
    if (!valid_channel(channel)) return 0.0f;
    return (float)dev->raw[channel] * kVoltsPerCount[dev->pga];
}

uint16_t ads1115_read_12bit(const ads1115_t *dev, int channel) {
    if (!valid_channel(channel)) return 0;
    int32_t raw = dev->raw[channel];
    if (raw <= 0) return 0; // a hair below ground reads negative; that's still "fully one way"

    // What a 12-bit 0-3.3V converter would have reported. Derived from the
    // PGA's counts-per-volt rather than hardcoded, so switching range
    // doesn't silently rescale every joystick on the board.
    float counts_per_volt = 1.0f / kVoltsPerCount[dev->pga];
    int32_t full_scale = (int32_t)(3.3f * counts_per_volt);
    if (full_scale <= 0) return 0;

    int32_t scaled = raw * 4095 / full_scale;
    if (scaled > 4095) scaled = 4095; // a supply a little above 3.3V, or a pot wired to it, overshoots
    return (uint16_t)scaled;
}

uint32_t ads1115_scan_period_us(const ads1115_t *dev) {
    int enabled = 0;
    for (int i = 0; i < ADS1115_CHANNEL_COUNT; i++) {
        if (dev->channel_mask & (1u << i)) enabled++;
    }
    return (uint32_t)enabled * ADS1115_CONVERSION_US;
}
