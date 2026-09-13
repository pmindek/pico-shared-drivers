#pragma once

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MPR121 12-key capacitive touch sensor, over i2c. Polled, not interrupt-
// driven - the IRQ pin (if you wire one up) is used internally to skip an
// i2c transaction when there's nothing new to read, not to actually
// interrupt anything; call mpr121_update() once per main-loop iteration,
// same convention as encoder_take_delta()/button_pressed().
//
// This driver does not call i2c_init() itself, and does not set up SDA/
// SCL's GPIO function or pull-ups - the caller must have already done
// that for whichever i2c instance is passed in. On RP2040/RP2350, one
// i2c instance's SDA/SCL signals can be muxed onto more than one physical
// GPIO pin pair at once (each instance has a whole set of candidate
// pins, not just one), so it's entirely normal for the MPR121 to share
// an already-initialized i2c bus with another device on different
// physical pins - in which case only this device's own two pins need
// gpio_set_function(..., GPIO_FUNC_I2C) + gpio_pull_up() calls, not a
// second i2c_init().

#define MPR121_NUM_ELECTRODES 12

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    int irq_pin;         // -1 if not wired up - see mpr121_update()
    uint16_t touch_state; // bit i = electrode i touched, as of the last mpr121_update()
} mpr121_t;

// Resets and configures the chip (standard filter/threshold/config
// register values - the manufacturer's own reference configuration, same
// as what every other MPR121 driver out there uses) and enables all 12
// electrodes in run mode.
//
// irq_pin: if >= 0, configured as an input with a pull-up (the MPR121's
// IRQ output is open-drain, active-low, and goes low whenever its touch
// status has changed since the last read) - mpr121_update() then only
// bothers with an i2c transaction when this pin is actually asserted,
// instead of unconditionally polling every call. Pass -1 to always poll
// over i2c instead.
//
// Returns false if the chip never acknowledges its address (bad wiring,
// not powered, or a wrong ADDR-pin strap - the default address most
// breakouts ship with, ADDR tied to GND, is 0x5A).
bool mpr121_init(mpr121_t *dev, i2c_inst_t *i2c, uint8_t addr, int irq_pin);

// Re-reads touch status from the chip if there's new data to read - either
// the IRQ pin says so, or unconditionally if irq_pin was -1 at init - and
// updates the cached state returned by mpr121_touched()/
// mpr121_electrode_touched(). A transient i2c error leaves the last-known
// state as-is rather than clearing it. Call once per main-loop iteration.
void mpr121_update(mpr121_t *dev);

// Bit i set = electrode i was touched as of the last mpr121_update() call
// - a plain cached read with no side effects, safe to call as often as
// you like between updates.
uint16_t mpr121_touched(mpr121_t *dev);

// Convenience single-electrode check (electrode must be 0-11 - always
// false outside that range).
bool mpr121_electrode_touched(mpr121_t *dev, int electrode);

#ifdef __cplusplus
}
#endif
