#include "mpr121.h"
#include "hardware/gpio.h"

// Register map - see the MPR121 datasheet, section 5 ("Register Table").
// Only the registers this driver actually touches are named.
#define MPR121_REG_TOUCHSTATUS_L 0x00
#define MPR121_REG_TOUCHSTATUS_H 0x01

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

    // Soft reset, then stop mode (ECR = 0) before (re)configuring - the
    // datasheet requires the chip to be in stop mode while the filter/
    // threshold/config registers below are written.
    if (!write_reg(dev, MPR121_REG_SOFTRESET, 0x63)) return false;
    sleep_ms(1);
    if (!write_reg(dev, MPR121_REG_ECR, 0x00)) return false;

    // Baseline filtering - the manufacturer's own recommended
    // configuration (every other MPR121 driver out there uses these same
    // values verbatim; there's no project-specific "why" beyond that).
    write_reg(dev, MPR121_REG_MHDR, 0x01);
    write_reg(dev, MPR121_REG_NHDR, 0x01);
    write_reg(dev, MPR121_REG_NCLR, 0x0E);
    write_reg(dev, MPR121_REG_FDLR, 0x00);
    write_reg(dev, MPR121_REG_MHDF, 0x01);
    write_reg(dev, MPR121_REG_NHDF, 0x05);
    write_reg(dev, MPR121_REG_NCLF, 0x01);
    write_reg(dev, MPR121_REG_FDLF, 0x00);
    write_reg(dev, MPR121_REG_NHDT, 0x00);
    write_reg(dev, MPR121_REG_NCLT, 0x00);
    write_reg(dev, MPR121_REG_FDLT, 0x00);

    for (int e = 0; e < MPR121_NUM_ELECTRODES; e++) {
        write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e, MPR121_DEFAULT_TOUCH_THRESHOLD);
        write_reg(dev, MPR121_REG_TOUCHTH_0 + 2 * e + 1, MPR121_DEFAULT_RELEASE_THRESHOLD);
    }

    write_reg(dev, MPR121_REG_DEBOUNCE, 0x00);
    write_reg(dev, MPR121_REG_CONFIG1, 0x10); // 16uA electrode charge current
    write_reg(dev, MPR121_REG_CONFIG2, 0x20); // 0.5us encoding, 1ms sample period

    // Run mode: baseline tracking enabled, all 12 electrodes active.
    if (!write_reg(dev, MPR121_REG_ECR, 0x8C)) return false;

    // Final sanity check: a chip that never ACKed any write above would
    // have already made this false, but a read costs nothing extra and
    // confirms it's genuinely alive, not just wired to something that
    // happens to ACK writes.
    uint8_t status[2];
    return read_regs(dev, MPR121_REG_TOUCHSTATUS_L, status, 2);
}

void mpr121_update(mpr121_t *dev) {
    if (dev->irq_pin >= 0 && gpio_get(dev->irq_pin)) return; // IRQ not asserted (active-low) - nothing new to read

    uint8_t status[2];
    if (!read_regs(dev, MPR121_REG_TOUCHSTATUS_L, status, 2)) return; // transient error - keep the last-known state
    dev->touch_state = (uint16_t)status[0] | (uint16_t)((status[1] & 0x0F) << 8);
}

uint16_t mpr121_touched(mpr121_t *dev) {
    return dev->touch_state;
}

bool mpr121_electrode_touched(mpr121_t *dev, int electrode) {
    if (electrode < 0 || electrode >= MPR121_NUM_ELECTRODES) return false;
    return (dev->touch_state & (1u << electrode)) != 0;
}
