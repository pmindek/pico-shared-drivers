#pragma once

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// AW9523B: 16 general-purpose I/O over i2c, where any pin can instead be a
// constant-current LED driver (256 brightness steps, no series resistor
// needed). Polled, same convention as this library's other drivers: call
// aw9523_update() once per main-loop iteration and then read the cached
// state as often as you like.
//
// This driver does not call i2c_init() and does not set up SDA/SCL's GPIO
// function or pull-ups - the caller owns the bus, exactly as in mpr121.h.
//
// THREE THINGS THAT BITE, all of them hardware, not software:
//
//   1. NO INTERNAL PULL-UPS OR PULL-DOWNS. Unlike most expanders, this chip
//      has none, so every input needs an external resistor - 10k to the
//      supply, with the switch pulling the pin to ground, matching the
//      active-low convention button.h/encoder.h/joystick.h already use. An
//      input left floating reads as noise, not as "released".
//
//   2. ITS ADDRESS RANGE OVERLAPS THE MPR121'S. The AW9523 answers at
//      0x58-0x5B (AD1/AD0), and an MPR121 answers at 0x5A-0x5D. On a board
//      with both, keep the AW9523 at 0x58 or 0x59 - i.e. never strap AD1 -
//      or the two will collide as soon as they share a bus.
//
//   3. AT POWER-ON, BEFORE aw9523_init() RUNS, every pin is an output
//      driving low. That is harmless for switches wired to ground and for
//      LEDs wired as this chip drives them, but it would fight anything
//      that drives a pin high.
//
// Pin numbering here is a single 0-15 range: 0-7 are the chip's P0_0..P0_7,
// 8-15 are P1_0..P1_7. Masks follow the same bit order, so bit 0 is P0_0
// and bit 15 is P1_7.

#define AW9523_PIN_COUNT 16

// AD1/AD0 both low. See note 2 above before changing it.
#define AW9523_DEFAULT_ADDR 0x58

// Maximum LED current, as a fraction of the chip's full range (which is
// about 37mA). Applies to every LED pin at once - per-pin brightness is
// set separately with aw9523_set_led(). Indicator LEDs need nowhere near
// the full range, and a lower limit is the easiest way to keep a
// battery-powered board's LED budget under control.
typedef enum {
    AW9523_LED_RANGE_FULL = 0,
    AW9523_LED_RANGE_THREE_QUARTER = 1,
    AW9523_LED_RANGE_HALF = 2,
    AW9523_LED_RANGE_QUARTER = 3,
} aw9523_led_range_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    uint16_t input_mask;  // 1 = this pin is an input
    uint16_t led_mask;    // 1 = this pin is a constant-current LED driver
    uint16_t inputs;      // bit i = pin i read high, as of the last aw9523_update()
    uint16_t outputs;     // shadow of the output registers - see aw9523_set_output()
} aw9523_t;

// Resets and configures the chip: every pin in input_mask becomes an input,
// every pin in led_mask becomes an LED driver (starting dark), and anything
// in neither becomes a push-pull output driving low. A pin in both masks is
// treated as an input.
//
// Returns false unless the chip both answers and identifies itself - the ID
// register has to read back the AW9523's own value, so something else that
// merely ACKs this address is reported as missing rather than half-working.
bool aw9523_init(aw9523_t *dev, i2c_inst_t *i2c, uint8_t addr, uint16_t input_mask, uint16_t led_mask);

// Re-reads both input ports in one transaction and updates the cached state
// behind aw9523_inputs()/aw9523_pin_high()/aw9523_pin_pressed(). Returns
// false only if the read actually failed, which lets a caller notice a run
// of failures and treat it as the bus needing recovery rather than this one
// read needing a retry - same contract as mpr121_update().
//
// This also clears a pending interrupt on the chip's INT pin, if one is
// wired up.
bool aw9523_update(aw9523_t *dev);

// Bit i = pin i read high at the last update. A plain cached read with no
// side effects.
uint16_t aw9523_inputs(const aw9523_t *dev);

// That pin's raw level at the last update.
bool aw9523_pin_high(const aw9523_t *dev, int pin);

// The same reading, inverted, for the usual switch-to-ground wiring: true
// means the switch is closed. Debouncing is the caller's job (button.h).
bool aw9523_pin_pressed(const aw9523_t *dev, int pin);

// Drives an output pin, for pins in neither mask. No-op returning false for
// an input or LED pin, so a wiring/config mismatch is visible rather than
// silent.
bool aw9523_set_output(aw9523_t *dev, int pin, bool high);

// Sets an LED pin's brightness, 0 (off) to 255 (the current limit set by
// aw9523_set_led_range()). Returns false for a pin that isn't in led_mask.
bool aw9523_set_led(aw9523_t *dev, int pin, uint8_t brightness);

// Changes the current limit shared by every LED pin. Safe to call at any
// time; brightness values already set keep their meaning relative to the
// new limit.
bool aw9523_set_led_range(aw9523_t *dev, aw9523_led_range_t range);

// Enables the chip's INT output for the pins in mask (1 = interrupt on
// change). Interrupts are all disabled by aw9523_init(), because this
// driver polls; wire INT and call this only if you want a GPIO interrupt to
// tell you when to poll. INT is open-drain and active-low, so several chips
// can share one line with a single pull-up.
bool aw9523_set_interrupt_mask(aw9523_t *dev, uint16_t mask);

#ifdef __cplusplus
}
#endif
