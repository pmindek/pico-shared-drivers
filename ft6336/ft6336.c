#include "ft6336.h"

// Register map (FocalTech FT6x36 application note).
#define REG_TD_STATUS 0x02 // number of touch points, low 4 bits
#define REG_P1_XH 0x03     // event flag (bits 7:6) + X high nibble; XL, YH, YL follow
#define REG_G_MODE 0xA4    // INT mode: 0x00 = held low while touched, 0x01 = a pulse per report
#define REG_CHIP_ID 0xA3   // 0x64 on an FT6336U
#define REG_FIRMWARE 0xA6
#define REG_VENDOR_ID 0xA8 // FocalTech's 0x11
#define REG_P_MODE 0xA5    // power mode: 0x00 active, 0x01 monitor, 0x03 hibernate

#define G_MODE_HELD_LOW 0x00
#define P_MODE_HIBERNATE 0x03

// P1_XH's event flag: 0 = put down, 1 = lifted, 2 = contact, 3 = none.
#define EVENT_LIFT_UP 1
#define EVENT_NONE 3

#define RESET_LOW_MS 5 // the datasheet's minimum reset pulse is shorter; 5 is a safe margin
#define I2C_TIMEOUT_US 2000

static bool read_regs(ft6336_t *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(dev->i2c, dev->addr, buf, len, false, I2C_TIMEOUT_US) == (int)len;
}

static bool write_reg(ft6336_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_write_timeout_us(dev->i2c, dev->addr, buf, 2, false, I2C_TIMEOUT_US) == 2;
}

void ft6336_setup(ft6336_t *dev, i2c_inst_t *i2c, uint8_t addr, int rst_pin, int int_pin)
{
    dev->i2c = i2c;
    dev->addr = addr;
    dev->rst_pin = rst_pin;
    dev->int_pin = int_pin;
    dev->present = false;
    dev->chip_id = dev->vendor_id = dev->firmware_version = 0;
    dev->errors = 0;

    if (int_pin >= 0) {
        gpio_init(int_pin);
        gpio_set_dir(int_pin, GPIO_IN);
        gpio_pull_up(int_pin);
    }
    if (rst_pin >= 0) {
        gpio_init(rst_pin);
        gpio_put(rst_pin, 0);
        gpio_set_dir(rst_pin, GPIO_OUT);
        sleep_ms(RESET_LOW_MS);
        gpio_put(rst_pin, 1);
    }
    dev->reset_released_us = time_us_64();
}

bool ft6336_probe(ft6336_t *dev)
{
    uint64_t ready_us = dev->reset_released_us + (uint64_t)FT6336_POWER_ON_MS * 1000;
    uint64_t now = time_us_64();
    if (now < ready_us) sleep_us(ready_us - now);

    uint8_t id;
    if (!read_regs(dev, REG_CHIP_ID, &id, 1)) {
        dev->present = false;
        return false;
    }
    dev->chip_id = id;
    read_regs(dev, REG_VENDOR_ID, &dev->vendor_id, 1);
    read_regs(dev, REG_FIRMWARE, &dev->firmware_version, 1);
    write_reg(dev, REG_G_MODE, G_MODE_HELD_LOW);
    dev->errors = 0;
    dev->present = true;
    return true;
}

bool ft6336_read(ft6336_t *dev, int *x, int *y)
{
    uint8_t buf[5]; // TD_STATUS, XH, XL, YH, YL
    if (!read_regs(dev, REG_TD_STATUS, buf, sizeof(buf))) {
        dev->errors++;
        return false;
    }
    int points = buf[0] & 0x0F;
    if (points == 0 || points > 2) return false; // >2 is the chip's "nothing valid" (0x0F after a reset)
    int event = buf[1] >> 6;
    if (event == EVENT_LIFT_UP || event == EVENT_NONE) return false;
    *x = ((buf[1] & 0x0F) << 8) | buf[2];
    *y = ((buf[3] & 0x0F) << 8) | buf[4];
    return true;
}

bool ft6336_int_asserted(const ft6336_t *dev)
{
    if (dev->int_pin < 0) return true;
    return !gpio_get(dev->int_pin);
}

bool ft6336_hibernate(ft6336_t *dev)
{
    bool ok = write_reg(dev, REG_P_MODE, P_MODE_HIBERNATE);
    dev->present = false; // until a reset and a probe bring it back
    return ok;
}

bool ft6336_powered_up(const ft6336_t *dev)
{
    return time_us_64() - dev->reset_released_us >= (uint64_t)FT6336_POWER_ON_MS * 1000;
}
