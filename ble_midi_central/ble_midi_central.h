#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// BLE-MIDI central: finds a BLE-MIDI peripheral, connects to it, and sends
// MIDI to it. The other half of bats' peripheral, and what lets a controller
// play a synth with no cable.
//
// Scope on purpose: this knows about MIDI and about one optional extra
// characteristic. It knows nothing about what any project streams through
// that characteristic - gyros sends an orientation quaternion, and this file
// never mentions quaternions. Anything device-specific (which axis maps to
// which CC, how often to send, what velocity a key press means) belongs to
// the caller.
//
// Everything here is best-effort: sends before a connection exists, or after
// it drops, are discarded rather than queued. A controller that stopped
// playing three seconds ago has nothing useful to say when it reconnects.
//
// Requires a project with BTstack and the CYW43 radio set up in CMake
// (pico_btstack_ble, pico_btstack_cyw43, one of the pico_cyw43_arch_*
// libraries) and a btstack_config.h with ENABLE_LE_CENTRAL.

// Starts the radio, then scans, connects and discovers, all in the
// background; returns immediately. Reconnects by itself if the link drops.
//
// target_name: connect only to a peripheral advertising exactly this name
// (bats advertises "Bats MIDI"). NULL connects to the first device
// advertising the standard BLE-MIDI service instead, which is how to reach
// a synth or wireless-MIDI dongle whose name you don't know.
//
// Call ble_midi_central_set_aux_service() BEFORE this if the extra
// characteristic is wanted - discovery order is fixed at connect time.
void ble_midi_central_init(const char *target_name);

// Also discover this custom 128-bit service and keep a handle on its first
// characteristic, for ble_midi_central_send_aux(). Optional: without it,
// only MIDI is discovered. A missing service is logged and skipped, not
// treated as a failed connection - losing an extra stream is no reason to
// give up a working MIDI link.
void ble_midi_central_set_aux_service(const uint8_t service_uuid128[16]);

// True once connected AND the MIDI characteristic has been found, i.e. once
// sends will actually go somewhere.
bool ble_midi_central_connected(void);
bool ble_midi_central_aux_ready(void);

// Velocity is the caller's business: a capacitive key has no real velocity
// and will pass a fixed value, while a proper keybed would pass a measured
// one. note_off sends a true Note Off status byte rather than a Note On with
// velocity 0 - both are valid MIDI, this one is unambiguous.
void ble_midi_central_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void ble_midi_central_note_off(uint8_t channel, uint8_t note);
void ble_midi_central_control_change(uint8_t channel, uint8_t controller, uint8_t value);

// 14-bit, 0 to 16383, where 8192 is centre.
void ble_midi_central_pitch_bend(uint8_t channel, uint16_t value);

// One complete MIDI message, status byte first, for anything without a
// helper above. Framed and sent as-is; at most 16 bytes.
void ble_midi_central_send(const uint8_t *message, uint8_t length);

// Raw bytes to the extra characteristic - see
// ble_midi_central_set_aux_service(). No framing, no interpretation.
void ble_midi_central_send_aux(const void *data, uint16_t length);

#ifdef __cplusplus
}
#endif
