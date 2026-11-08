#pragma once

// BLE MIDI client (central) for the M-VAVE FM-1 running CTL-1.
//
// Scans actively for a device named "FM-1" or advertising the BLE MIDI service
// (the name or the UUID may be in the advertisement or the scan response),
// prefers the last connected address saved in NVS, connects with a short
// connection interval, exchanges MTU, finds the MIDI I/O characteristic and
// subscribes to it. Reconnects by itself after a drop.
//
// Also implements midi_out_send() (midi_out.h): messages are packed into
// BLE MIDI packets (header + per-message timestamp, up to MTU - 3 bytes) and
// queued for a sender task that writes them without response, backing off
// while the link is busy.

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    BLE_MIDI_SCANNING = 0,
    BLE_MIDI_CONNECTING,
    BLE_MIDI_CONNECTED,
    BLE_MIDI_DISCONNECTED,
} ble_midi_state_t;

// Called from the BLE host task (or a timer task). `name` is the device name
// when connected, else NULL. Do not block for long in it.
typedef void (*ble_midi_state_cb_t)(ble_midi_state_t state, const char *name);

// Needs nvs_flash_init() done first
esp_err_t ble_midi_start(ble_midi_state_cb_t cb);

bool ble_midi_is_connected(void);
