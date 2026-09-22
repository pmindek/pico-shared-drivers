#include "aw9523.h"

// Register map - see the AW9523B datasheet. Only the registers this driver
// actually touches are named.
#define AW9523_REG_INPUT0 0x00    // + 1 = INPUT1; reads auto-increment, so both come back in one transaction
#define AW9523_REG_OUTPUT0 0x02   // + 1 = OUTPUT1
#define AW9523_REG_CONFIG0 0x04   // + 1 = CONFIG1. Bit set = INPUT, clear = output (inverted from what you might expect)
#define AW9523_REG_INTENABLE0 0x06 // + 1 = INTENABLE1. Bit set = interrupt DISABLED for that pin
#define AW9523_REG_CHIPID 0x10
#define AW9523_REG_GCR 0x11       // bit 4 = port 0 push-pull (else open-drain); bits 1:0 = LED current range
#define AW9523_REG_LEDMODE0 0x12  // + 1 = LEDMODE1. Bit set = GPIO mode, clear = constant-current LED mode
#define AW9523_REG_SOFTRESET 0x7F // any write resets the chip

#define AW9523_CHIPID 0x23

#define AW9523_GCR_P0_PUSH_PULL (1u << 4)

// i2c_write_blocking()/i2c_read_blocking() have no timeout on this SDK - a
// chip that never ACKs (bad wiring, not powered, wrong address strap) hangs
// them forever instead of failing. Same reasoning and value as mpr121.c.
#define AW9523_I2C_TIMEOUT_US 50000

// The per-pin brightness registers are NOT in pin order, and not in one
// contiguous run: P0_0..P0_7 sit at 0x24..0x2B, P1_0..P1_3 at 0x20..0x23,
// and P1_4..P1_7 at 0x2C..0x2F. Getting this wrong lights the wrong LED,
// which looks like a wiring fault and wastes an afternoon, so it lives in
// one place.
static uint8_t dim_register(int pin) {
    if (pin <= 7) return (uint8_t)(0x24 + pin);       // P0_0..P0_7
    if (pin <= 11) return (uint8_t)(0x20 + pin - 8);  // P1_0..P1_3
    return (uint8_t)(0x2C + pin - 12);                // P1_4..P1_7
}

static bool write_reg(aw9523_t *dev, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_write_timeout_us(dev->i2c, dev->addr, buf, 2, false, AW9523_I2C_TIMEOUT_US) == 2;
}

static bool read_regs(aw9523_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, AW9523_I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(dev->i2c, dev->addr, data, len, false, AW9523_I2C_TIMEOUT_US) == (int)len;
}

static bool valid_pin(int pin) {
    return pin >= 0 && pin < AW9523_PIN_COUNT;
}

bool aw9523_init(aw9523_t *dev, i2c_inst_t *i2c, uint8_t addr, uint16_t input_mask, uint16_t led_mask) {
    dev->i2c = i2c;
    dev->addr = addr;
    dev->input_mask = input_mask;
    dev->led_mask = (uint16_t)(led_mask & ~input_mask); // an input wins, so a mask mistake can't drive current into a switch
    dev->inputs = 0;
    dev->outputs = 0;

    // Reset first, so a re-init (after a bus recovery, say) starts from the
    // same place as a cold boot rather than from whatever half-state the
    // chip was left in.
    if (!write_reg(dev, AW9523_REG_SOFTRESET, 0x00)) return false;
    sleep_ms(1);

    // Identify it properly. Every write above and below would also succeed
    // against some unrelated chip that happens to ACK this address; only the
    // ID register says it's really an AW9523. Same reasoning as the MPR121
    // driver's config-register check.
    uint8_t id = 0;
    if (!read_regs(dev, AW9523_REG_CHIPID, &id, 1)) return false;
    if (id != AW9523_CHIPID) return false;

    bool ok = true;

    // Port 0 defaults to open-drain, which can't source current and so
    // can't drive a plain output high. Push-pull makes both ports behave
    // the same. (Port 1 is always push-pull.)
    ok &= write_reg(dev, AW9523_REG_GCR, AW9523_GCR_P0_PUSH_PULL | (uint8_t)AW9523_LED_RANGE_QUARTER);

    // Outputs start low, before anything is switched away from GPIO mode,
    // so no pin briefly drives an unintended level.
    ok &= write_reg(dev, AW9523_REG_OUTPUT0, 0x00);
    ok &= write_reg(dev, AW9523_REG_OUTPUT0 + 1, 0x00);

    // 1 = GPIO, 0 = constant-current LED - inverted, hence the complement.
    ok &= write_reg(dev, AW9523_REG_LEDMODE0, (uint8_t)~(dev->led_mask & 0xFF));
    ok &= write_reg(dev, AW9523_REG_LEDMODE0 + 1, (uint8_t)~((dev->led_mask >> 8) & 0xFF));

    // 1 = input, 0 = output. LED pins are left as outputs here; the LED-mode
    // registers above are what actually takes them over.
    ok &= write_reg(dev, AW9523_REG_CONFIG0, (uint8_t)(dev->input_mask & 0xFF));
    ok &= write_reg(dev, AW9523_REG_CONFIG0 + 1, (uint8_t)((dev->input_mask >> 8) & 0xFF));

    // 1 = interrupt disabled. This driver polls, so everything starts
    // disabled and a caller opts in with aw9523_set_interrupt_mask(). An
    // interrupt nobody services just holds the shared INT line low forever.
    ok &= write_reg(dev, AW9523_REG_INTENABLE0, 0xFF);
    ok &= write_reg(dev, AW9523_REG_INTENABLE0 + 1, 0xFF);

    // Every LED starts dark rather than at whatever the reset left, so a
    // board doesn't flash on boot.
    for (int pin = 0; pin < AW9523_PIN_COUNT; pin++) {
        if (!(dev->led_mask & (1u << pin))) continue;
        ok &= write_reg(dev, dim_register(pin), 0x00);
    }

    if (!ok) return false;

    // One real read before declaring success, and it seeds the cached input
    // state so the first aw9523_pin_pressed() after init isn't a guess.
    return aw9523_update(dev);
}

bool aw9523_update(aw9523_t *dev) {
    uint8_t ports[2];
    if (!read_regs(dev, AW9523_REG_INPUT0, ports, 2)) return false; // transient error - keep the last-known state
    dev->inputs = (uint16_t)ports[0] | (uint16_t)(ports[1] << 8);
    return true;
}

uint16_t aw9523_inputs(const aw9523_t *dev) {
    return dev->inputs;
}

bool aw9523_pin_high(const aw9523_t *dev, int pin) {
    if (!valid_pin(pin)) return false;
    return (dev->inputs & (1u << pin)) != 0;
}

bool aw9523_pin_pressed(const aw9523_t *dev, int pin) {
    if (!valid_pin(pin)) return false;
    return (dev->inputs & (1u << pin)) == 0; // active low - switch to ground, external pull-up
}

bool aw9523_set_output(aw9523_t *dev, int pin, bool high) {
    if (!valid_pin(pin)) return false;
    uint16_t bit = (uint16_t)(1u << pin);
    if ((dev->input_mask | dev->led_mask) & bit) return false;

    uint16_t updated = high ? (uint16_t)(dev->outputs | bit) : (uint16_t)(dev->outputs & ~bit);
    uint8_t reg = (pin < 8) ? AW9523_REG_OUTPUT0 : (uint8_t)(AW9523_REG_OUTPUT0 + 1);
    uint8_t value = (pin < 8) ? (uint8_t)(updated & 0xFF) : (uint8_t)((updated >> 8) & 0xFF);
    if (!write_reg(dev, reg, value)) return false;

    // Only commit the shadow once the write actually landed, so a failed
    // write doesn't leave this driver believing a pin changed when it
    // didn't - the next call would then skip nothing and re-send anyway.
    dev->outputs = updated;
    return true;
}

bool aw9523_set_led(aw9523_t *dev, int pin, uint8_t brightness) {
    if (!valid_pin(pin)) return false;
    if (!(dev->led_mask & (1u << pin))) return false;
    return write_reg(dev, dim_register(pin), brightness);
}

bool aw9523_set_led_range(aw9523_t *dev, aw9523_led_range_t range) {
    return write_reg(dev, AW9523_REG_GCR, AW9523_GCR_P0_PUSH_PULL | (uint8_t)(range & 0x03));
}

bool aw9523_set_interrupt_mask(aw9523_t *dev, uint16_t mask) {
    uint16_t disabled = (uint16_t)~mask; // the register's 1 means disabled
    bool ok = write_reg(dev, AW9523_REG_INTENABLE0, (uint8_t)(disabled & 0xFF));
    ok &= write_reg(dev, AW9523_REG_INTENABLE0 + 1, (uint8_t)((disabled >> 8) & 0xFF));
    return ok;
}
