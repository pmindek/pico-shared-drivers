#pragma once

#include "pico/stdlib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rotary encoder (KY-040 style: CLK, DT, SW). Rotation is decoded by a full
// 4-step quadrature state machine, interrupt-driven on both CLK and DT (all
// edges), so clicks aren't missed even if the main loop is busy elsewhere
// (e.g. an I2C display push) - and, just as importantly, so a real
// mechanical encoder's contact bounce can't produce a wrong-direction or
// missed click. A click is only counted once a complete 3->1->0->2->3 (CW)
// or 3->2->0->1->3 (CCW) sequence lands cleanly back at rest; any
// out-of-sequence transition (bounce, or a step getting lost) just resets
// the in-progress count rather than corrupting it. SW (push button) is
// polled instead - same convention as bats/gyros' existing rezero-button
// code: call encoder_button_pressed() once per main-loop iteration.
//
// Direction (CW = positive delta) is the standard assumption for this
// wiring but hasn't been verified against real hardware here - if it comes
// out backwards, either negate the value from encoder_take_delta() at the
// call site, or swap the clk_pin/dt_pin arguments to encoder_init().
//
// Supports a handful of simultaneous encoders (see ENCODER_MAX_INSTANCES),
// sharing the one GPIO IRQ callback slot the Pico SDK allows per core -
// encoder_init() sets this up automatically, callers don't need to think
// about it.

#define ENCODER_MAX_INSTANCES 4

typedef struct {
    uint clk_pin;
    uint dt_pin;
    uint sw_pin;
    volatile uint8_t pin_state; // current 2-bit (CLK<<1 | DT) state
    volatile int8_t accum;      // steps accumulated since the last rest position
    volatile int delta;         // net completed clicks since the last encoder_take_delta()
    bool sw_was_pressed;
    uint64_t sw_last_change_us;
} encoder_t;

// Initializes CLK/DT/SW GPIOs (internal pull-ups; SW reads active-low, wire
// it to GND when pressed) and registers the CLK interrupt. `encoder` must
// stay valid for as long as the encoder is used - its address is kept, not
// copied.
void encoder_init(encoder_t *encoder, uint clk_pin, uint dt_pin, uint sw_pin);

// Returns the net rotation (in detents, +1 per CW click) accumulated since
// the last call, and resets it to 0.
int encoder_take_delta(encoder_t *encoder);

// True once per physical button press (on the press edge, debounced), not
// while held.
bool encoder_button_pressed(encoder_t *encoder);

#ifdef __cplusplus
}
#endif
