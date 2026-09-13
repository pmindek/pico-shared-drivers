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
// that for whichever i2c instance is passed in, typically because the
// MPR121 is sharing a bus another device already brought up (e.g. an
// IMU). Share it on that device's *own* SDA/SCL pins, not a second GPIO
// pair mapped to the same i2c instance - each RP2040/RP2350 i2c instance
// does have several candidate GPIO pairs it can be routed to, but
// configuring more than one pair for the same instance at once does not
// reliably make them behave as one wired-together bus in practice
// (observed on real hardware: the second pair configured can take over
// as the instance's actual input source, breaking whatever was already
// working on the first pair, even with nothing else wrong). If two
// devices need to share one i2c instance, wire them to the exact same
// two physical pins/nets.

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

// Live-adjusts every electrode's touch/release thresholds (mpr121_init()
// sets both to a reasonable manufacturer-recommended default - see
// MPR121_DEFAULT_TOUCH_THRESHOLD/RELEASE_THRESHOLD in mpr121.c - this
// overrides that). Unlike the filter/config registers mpr121_init() sets
// up once, the datasheet allows the threshold registers to be written at
// any time, including while the chip is already running in normal
// operation - no re-init needed, safe to call from a live control (e.g. an
// encoder) as often as you like. Lower thresholds = more sensitive
// (registers touch/release sooner, but more prone to false triggers from
// noise); release_threshold should stay below touch_threshold for
// hysteresis (avoids chatter right at the boundary) - the driver's own
// defaults use a 2:1 ratio (12/6) as a reasonable starting point.
void mpr121_set_thresholds(mpr121_t *dev, uint8_t touch_threshold, uint8_t release_threshold);

#ifdef __cplusplus
}
#endif
