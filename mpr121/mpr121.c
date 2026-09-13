#include "mpr121.h"
#include "hardware/gpio.h"

// Register map - see the MPR121 datasheet, section 5 ("Register Table").
// Only the registers this driver actually touches are named.
#define MPR121_REG_TOUCHSTATUS_L 0x00
#define MPR121_REG_TOUCHSTATUS_H 0x01

#define MPR121_REG_FILTERED_DATA_0 0x04 // + 2*electrode, 2 bytes LE per electrode (10-bit value)
#define MPR121_REG_BASELINE_0 0x1E      // + electrode, 1 byte per electrode - actual 10-bit baseline is this << 2

#define MPR121_REG_MHDR 0x2B
#define MPR121_REG_NHDR 0x2C
#define MPR121_REG_NCLR 0x2D
#define MPR121_REG_FDLR 0x2E
#define MPR121_REG_MHDF 0x2F
#define MPR121_REG_NHDF 0x30
#define MPR121_REG_NCLF 0x31
#define MPR121_REG_FDLF 0x32
#define MPR121_REG_NHDT 0x33
#define MPR121_REG_NCLT 0x34
#define MPR121_REG_FDLT 0x35

#define MPR121_REG_TOUCHTH_0 0x41 // + 2*electrode = touch threshold; +1 = that electrode's release threshold
#define MPR121_REG_DEBOUNCE 0x5B
#define MPR121_REG_CONFIG1 0x5C
#define MPR121_REG_CONFIG2 0x5D
#define MPR121_REG_ECR 0x5E // Electrode Configuration Register - electrode count + run/stop mode
#define MPR121_REG_SOFTRESET 0x80

#define MPR121_DEFAULT_TOUCH_THRESHOLD 12
#define MPR121_DEFAULT_RELEASE_THRESHOLD 6

// i2c_write_blocking()/i2c_read_blocking() have no timeout on this SDK - a
// chip that never ACKs (bad wiring, not powered, wrong address strap)
// hangs them forever instead of failing. Same reasoning/timeout value as
// gyros' hardware.cpp uses for the BMI160.
#define MPR121_I2C_TIMEOUT_US 50000

static bool write_reg(mpr121_t *dev, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_write_timeout_us(dev->i2c, dev->addr, buf, 2, false, MPR121_I2C_TIMEOUT_US) == 2;
}

static bool read_regs(mpr121_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, MPR121_I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(dev->i2c, dev->addr, data, (size_t)len, false, MPR121_I2C_TIMEOUT_US) == (int)len;
}

void mpr121_set_thresholds(mpr121_t *dev, uint8_t touch_threshold, uint8_t release_threshold) {
    for (int e = 0; e < MPR121_NUM_ELECTRODES; e++) {
        write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e, touch_threshold);
        write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e + 1, release_threshold);
    }
}

bool mpr121_init(mpr121_t *dev, i2c_inst_t *i2c, uint8_t addr, int irq_pin) {
    dev->i2c = i2c;
    dev->addr = addr;
    dev->irq_pin = irq_pin;
    dev->touch_state = 0;

    if (irq_pin >= 0) {
        gpio_init(irq_pin);
        gpio_set_dir(irq_pin, GPIO_IN);
        gpio_pull_up(irq_pin);
    }

    // Every write below is now checked (`ok &=`) and the whole function
    // fails if any of them didn't ACK, instead of only checking the first
    // (SOFTRESET), the last (ECR run-mode) and the final readback. That
    // used to mean a single dropped write in the middle - e.g. a brief
    // i2c glitch right as the chip's power rail was still settling at
    // boot - could leave the baseline-filter config half-applied (chip's
    // power-on-reset default in that register instead of the value meant
    // to land there) while mpr121_init() still reported success, because
    // the checked steps at the very start and very end both happened to
    // go through fine. Observed on real hardware as "found (OK) but
    // never registers a single touch, at any sensitivity" - not a wiring
    // or threshold problem, a badly-configured-but-technically-alive
    // chip that only a fresh, fully successful init sequence fixes (the
    // retry loop in gyros' hardware.cpp's touch_init() is what actually
    // gets a fully-successful sequence, this is just what lets it tell
    // a partial failure apart from a real success).
    bool ok = true;

    // Soft reset, then stop mode (ECR = 0) before (re)configuring - the
    // datasheet requires the chip to be in stop mode while the filter/
    // threshold/config registers below are written.
    ok &= write_reg(dev, MPR121_REG_SOFTRESET, 0x63);
    sleep_ms(1);
    ok &= write_reg(dev, MPR121_REG_ECR, 0x00);

    // Baseline filtering - the manufacturer's own recommended
    // configuration (every other MPR121 driver out there uses these same
    // values verbatim; there's no project-specific "why" beyond that).
    ok &= write_reg(dev, MPR121_REG_MHDR, 0x01);
    ok &= write_reg(dev, MPR121_REG_NHDR, 0x01);
    ok &= write_reg(dev, MPR121_REG_NCLR, 0x0E);
    ok &= write_reg(dev, MPR121_REG_FDLR, 0x00);
    ok &= write_reg(dev, MPR121_REG_MHDF, 0x01);
    ok &= write_reg(dev, MPR121_REG_NHDF, 0x05);
    ok &= write_reg(dev, MPR121_REG_NCLF, 0x01);
    ok &= write_reg(dev, MPR121_REG_FDLF, 0x00);
    ok &= write_reg(dev, MPR121_REG_NHDT, 0x00);
    ok &= write_reg(dev, MPR121_REG_NCLT, 0x00);
    ok &= write_reg(dev, MPR121_REG_FDLT, 0x00);

    for (int e = 0; e < MPR121_NUM_ELECTRODES; e++) {
        ok &= write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e, MPR121_DEFAULT_TOUCH_THRESHOLD);
        ok &= write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e + 1, MPR121_DEFAULT_RELEASE_THRESHOLD);
    }

    // Debounce: require 2 consecutive consistent samples before a
    // touch/release actually registers (bits 0-2 = touch debounce, bits
    // 4-6 = release debounce - 0x22 sets both to 2). Was 0x00 (no
    // debounce at all) - fine for a real touch (which is a sustained
    // contact, easily surviving a couple extra samples of confirmation),
    // but on unshielded breadboard wiring a single noisy ADC sample is
    // enough to register a phantom touch/release with no debounce at
    // all - observed as electrodes firing on their own with nothing
    // touching them.
    ok &= write_reg(dev, MPR121_REG_DEBOUNCE, 0x22);
    ok &= write_reg(dev, MPR121_REG_CONFIG1, 0x10); // 16uA electrode charge current
    ok &= write_reg(dev, MPR121_REG_CONFIG2, 0x20); // 0.5us encoding, 1ms sample period

    // Run mode: baseline tracking enabled, all 12 electrodes active.
    ok &= write_reg(dev, MPR121_REG_ECR, 0x8C);

    if (!ok) return false;

    // Final sanity check: a chip that never ACKed any write above would
    // have already made this false, but a read costs nothing extra and
    // confirms it's genuinely alive, not just wired to something that
    // happens to ACK writes.
    uint8_t status[2];
    return read_regs(dev, MPR121_REG_TOUCHSTATUS_L, status, 2);
}

bool mpr121_update(mpr121_t *dev) {
    if (dev->irq_pin >= 0 && gpio_get(dev->irq_pin)) return true; // IRQ not asserted (active-low) - nothing new to read, not a failure

    uint8_t status[2];
    if (!read_regs(dev, MPR121_REG_TOUCHSTATUS_L, status, 2)) return false; // transient error - keep the last-known state
    dev->touch_state = (uint16_t)status[0] | (uint16_t)((status[1] & 0x0F) << 8);
    return true;
}

uint16_t mpr121_touched(mpr121_t *dev) {
    return dev->touch_state;
}

bool mpr121_electrode_touched(mpr121_t *dev, int electrode) {
    if (electrode < 0 || electrode >= MPR121_NUM_ELECTRODES) return false;
    return (dev->touch_state & (1u << electrode)) != 0;
}

bool mpr121_read_diagnostics(mpr121_t *dev, uint16_t *filtered, uint8_t *baseline) {
    uint8_t filtered_raw[2 * MPR121_NUM_ELECTRODES];
    if (!read_regs(dev, MPR121_REG_FILTERED_DATA_0, filtered_raw, sizeof(filtered_raw))) return false;
    if (!read_regs(dev, MPR121_REG_BASELINE_0, baseline, MPR121_NUM_ELECTRODES)) return false;
    for (int e = 0; e < MPR121_NUM_ELECTRODES; e++) {
        filtered[e] = (uint16_t)filtered_raw[2 * e] | ((uint16_t)(filtered_raw[2 * e + 1] & 0x03) << 8);
    }
    return true;
}
