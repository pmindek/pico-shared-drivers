#include "ble_midi_central.h"

#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"

// The BLE-MIDI service, as the MIDI Manufacturers Association defines it:
// 03B80E5A-EDE8-4B33-A751-6CE34EC4C700. Every BLE-MIDI peripheral advertises
// it, which is what makes "connect to any synth" possible without knowing
// its name.
static const uint8_t kMidiServiceUuid[16] = {
    0x03, 0xB8, 0x0E, 0x5A, 0xED, 0xE8, 0x4B, 0x33, 0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00,
};

// A BLE-MIDI write is a header byte, a timestamp byte, then MIDI. Both of
// those carry a 13-bit millisecond timestamp split across them, which lets a
// receiver reconstruct the timing of several messages batched into one
// packet. Sending one message per write, as here, makes that machinery
// pointless, so both bytes are the minimum valid value: 0x80 is "high bit
// set, timestamp zero". Receivers treat it as "now", which is what we mean.
#define BLE_MIDI_HEADER 0x80
#define BLE_MIDI_TIMESTAMP 0x80

#define BLE_MIDI_MAX_MESSAGE 16

enum stage {
    STAGE_IDLE,
    STAGE_SCANNING,
    STAGE_CONNECTING,
    STAGE_DISCOVERING_AUX_SERVICE,
    STAGE_DISCOVERING_AUX_CHARACTERISTIC,
    STAGE_DISCOVERING_MIDI_SERVICE,
    STAGE_DISCOVERING_MIDI_CHARACTERISTIC,
    STAGE_READY,
};

static btstack_packet_callback_registration_t s_hci_event_callback_registration;
static enum stage s_stage = STAGE_IDLE;
static hci_con_handle_t s_connection_handle = HCI_CON_HANDLE_INVALID;
static gatt_client_service_t s_service;
static bool s_service_found;

static const char *s_target_name; // NULL = match the MIDI service instead
static bool s_want_aux;
static uint8_t s_aux_service_uuid[16];
static uint16_t s_aux_value_handle;
static uint16_t s_midi_value_handle;

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void start_scan(void)
{
    s_stage = STAGE_SCANNING;
    s_connection_handle = HCI_CON_HANDLE_INVALID;
    s_service_found = false;
    s_aux_value_handle = 0;
    s_midi_value_handle = 0;
    gap_start_scan();
    if (s_target_name) {
        printf("BLE MIDI: scanning for \"%s\"...\n", s_target_name);
    } else {
        printf("BLE MIDI: scanning for any BLE-MIDI device...\n");
    }
}

static bool advertisement_matches(const uint8_t *data, uint8_t data_len)
{
    ad_context_t context;
    for (ad_iterator_init(&context, data_len, data); ad_iterator_has_more(&context); ad_iterator_next(&context)) {
        uint8_t type = ad_iterator_get_data_type(&context);
        uint8_t len = ad_iterator_get_data_len(&context);
        const uint8_t *value = ad_iterator_get_data(&context);

        if (s_target_name) {
            if (type != BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME) continue;
            if (len == strlen(s_target_name) && memcmp(value, s_target_name, len) == 0) return true;
            continue;
        }

        // No name given: accept anything advertising the BLE-MIDI service.
        // The UUID goes out little-endian on the wire, so it is compared
        // reversed rather than byte-for-byte.
        if (type != BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS &&
            type != BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS) {
            continue;
        }
        for (uint8_t offset = 0; offset + 16 <= len; offset += 16) {
            bool same = true;
            for (int i = 0; i < 16; i++) {
                if (value[offset + i] != kMidiServiceUuid[15 - i]) {
                    same = false;
                    break;
                }
            }
            if (same) return true;
        }
    }
    return false;
}

// Every BTstack call from outside its own callbacks goes through here.
//
// BTstack is not thread-safe against its own background processing, which
// runs in the CYW43 driver's async context. Calling into it from ordinary
// main-loop code - a screen sending a note, say - races that processing and
// corrupts BTstack's internal state. That showed up as the whole board
// crashing at random, worst when several keys were pressed and released
// close together, because that squeezes more calls into the same window.
//
// Taking the async context's own lock is the fix, and it is the same lock
// cyw43_arch_lwip_begin() takes - that function is named for lwIP but is
// really just this. Using the async context directly says what is meant and
// works whether or not the project has lwIP at all. The lock is recursive,
// so calling this from inside a BTstack callback is safe too.
static void write_characteristic(uint16_t value_handle, const uint8_t *data, uint16_t length)
{
    if (s_stage != STAGE_READY || value_handle == 0) return; // best-effort: dropped, not queued

    async_context_t *context = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(context);
    gatt_client_write_value_of_characteristic_without_response(s_connection_handle, value_handle, length,
                                                               (uint8_t *)data);
    async_context_release_lock(context);
}

void ble_midi_central_send(const uint8_t *message, uint8_t length)
{
    if (length == 0 || length > BLE_MIDI_MAX_MESSAGE) return;

    uint8_t packet[2 + BLE_MIDI_MAX_MESSAGE];
    packet[0] = BLE_MIDI_HEADER;
    packet[1] = BLE_MIDI_TIMESTAMP;
    memcpy(&packet[2], message, length);
    write_characteristic(s_midi_value_handle, packet, (uint16_t)(length + 2));
}

void ble_midi_central_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t message[3] = {(uint8_t)(0x90 | (channel & 0x0F)), (uint8_t)(note & 0x7F), (uint8_t)(velocity & 0x7F)};
    ble_midi_central_send(message, sizeof(message));
}

void ble_midi_central_note_off(uint8_t channel, uint8_t note)
{
    uint8_t message[3] = {(uint8_t)(0x80 | (channel & 0x0F)), (uint8_t)(note & 0x7F), 0};
    ble_midi_central_send(message, sizeof(message));
}

void ble_midi_central_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    uint8_t message[3] = {(uint8_t)(0xB0 | (channel & 0x0F)), (uint8_t)(controller & 0x7F), (uint8_t)(value & 0x7F)};
    ble_midi_central_send(message, sizeof(message));
}

void ble_midi_central_pitch_bend(uint8_t channel, uint16_t value)
{
    if (value > 16383) value = 16383;
    uint8_t message[3] = {(uint8_t)(0xE0 | (channel & 0x0F)), (uint8_t)(value & 0x7F), (uint8_t)((value >> 7) & 0x7F)};
    ble_midi_central_send(message, sizeof(message));
}

void ble_midi_central_send_aux(const void *data, uint16_t length)
{
    write_characteristic(s_aux_value_handle, (const uint8_t *)data, length);
}

bool ble_midi_central_connected(void)
{
    return s_stage == STAGE_READY && s_midi_value_handle != 0;
}

bool ble_midi_central_aux_ready(void)
{
    return s_stage == STAGE_READY && s_aux_value_handle != 0;
}

void ble_midi_central_set_aux_service(const uint8_t service_uuid128[16])
{
    memcpy(s_aux_service_uuid, service_uuid128, sizeof(s_aux_service_uuid));
    s_want_aux = true;
}

static void discover_midi_service(void)
{
    s_stage = STAGE_DISCOVERING_MIDI_SERVICE;
    s_service_found = false;
    gatt_client_discover_primary_services_by_uuid128(&handle_gatt_client_event, s_connection_handle, kMidiServiceUuid);
}

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case GATT_EVENT_SERVICE_QUERY_RESULT:
            gatt_event_service_query_result_get_service(packet, &s_service);
            s_service_found = true;
            break;

        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
            gatt_client_characteristic_t characteristic;
            gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
            if (s_stage == STAGE_DISCOVERING_AUX_CHARACTERISTIC) {
                s_aux_value_handle = characteristic.value_handle;
            } else if (s_stage == STAGE_DISCOVERING_MIDI_CHARACTERISTIC) {
                s_midi_value_handle = characteristic.value_handle;
            }
            break;
        }

        case GATT_EVENT_QUERY_COMPLETE: {
            uint8_t status = gatt_event_query_complete_get_att_status(packet);
            if (status != ATT_ERROR_SUCCESS) {
                printf("BLE MIDI: GATT query failed (status 0x%02x), disconnecting.\n", status);
                gap_disconnect(s_connection_handle);
                break;
            }

            switch (s_stage) {
                // The extra characteristic is optional all the way through:
                // if it isn't there, say so and carry on to MIDI rather than
                // dropping a link that would have worked for notes.
                case STAGE_DISCOVERING_AUX_SERVICE:
                    if (!s_service_found) {
                        printf("BLE MIDI: extra service not found, continuing with MIDI only.\n");
                        discover_midi_service();
                        break;
                    }
                    s_stage = STAGE_DISCOVERING_AUX_CHARACTERISTIC;
                    gatt_client_discover_characteristics_for_service(&handle_gatt_client_event, s_connection_handle,
                                                                    &s_service);
                    break;

                case STAGE_DISCOVERING_AUX_CHARACTERISTIC:
                    if (s_aux_value_handle == 0) {
                        printf("BLE MIDI: extra characteristic not found, continuing with MIDI only.\n");
                    }
                    discover_midi_service();
                    break;

                case STAGE_DISCOVERING_MIDI_SERVICE:
                    if (!s_service_found) {
                        printf("BLE MIDI: no MIDI service on this device, disconnecting.\n");
                        gap_disconnect(s_connection_handle);
                        break;
                    }
                    s_stage = STAGE_DISCOVERING_MIDI_CHARACTERISTIC;
                    gatt_client_discover_characteristics_for_service(&handle_gatt_client_event, s_connection_handle,
                                                                    &s_service);
                    break;

                case STAGE_DISCOVERING_MIDI_CHARACTERISTIC:
                    if (s_midi_value_handle == 0) {
                        printf("BLE MIDI: MIDI characteristic not found, disconnecting.\n");
                        gap_disconnect(s_connection_handle);
                        break;
                    }
                    s_stage = STAGE_READY;
                    printf("BLE MIDI: connected and ready%s.\n", s_aux_value_handle ? " (with extra characteristic)" : "");
                    break;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case GAP_EVENT_ADVERTISING_REPORT: {
            if (s_stage != STAGE_SCANNING) break; // already connecting or connected

            uint8_t data_len = gap_event_advertising_report_get_data_length(packet);
            const uint8_t *data = gap_event_advertising_report_get_data(packet);
            if (!advertisement_matches(data, data_len)) break;

            bd_addr_t address;
            gap_event_advertising_report_get_address(packet, address);
            bd_addr_type_t address_type = (bd_addr_type_t)gap_event_advertising_report_get_address_type(packet);

            printf("BLE MIDI: found a device, connecting...\n");
            s_stage = STAGE_CONNECTING;
            gap_stop_scan();
            gap_connect(address, address_type);
            break;
        }

        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                s_connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                uint8_t status;
                if (s_want_aux) {
                    s_stage = STAGE_DISCOVERING_AUX_SERVICE;
                    status = gatt_client_discover_primary_services_by_uuid128(&handle_gatt_client_event,
                                                                             s_connection_handle, s_aux_service_uuid);
                } else {
                    s_stage = STAGE_DISCOVERING_MIDI_SERVICE;
                    s_service_found = false;
                    status = gatt_client_discover_primary_services_by_uuid128(&handle_gatt_client_event,
                                                                             s_connection_handle, kMidiServiceUuid);
                }
                if (status != ERROR_CODE_SUCCESS) {
                    printf("BLE MIDI: service discovery failed to start (status 0x%02x)\n", status);
                }
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("BLE MIDI: disconnected, rescanning.\n");
            start_scan();
            break;

        default:
            break;
    }
}

void ble_midi_central_init(const char *target_name)
{
    s_target_name = target_name;

    btstack_cyw43_init(cyw43_arch_async_context());

    l2cap_init();
    sm_init();
    gatt_client_init(); // must come after sm_init(): it registers the GATT client's own HCI
                        // event handler, without which discovery and writes silently do nothing

    s_hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&s_hci_event_callback_registration);

    gap_set_scan_params(1 /* active, so scan responses carry the name */, 0x0030, 0x0030, 0 /* no whitelist */);
    start_scan();

    hci_power_control(HCI_POWER_ON);
}
