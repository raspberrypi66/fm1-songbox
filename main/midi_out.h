#pragma once

// MIDI output used by the player. Implemented by the BLE MIDI client
// (ble_midi.c); drops everything while the FM-1 is not connected.

#include "midi_event.h"

// Messages due at the same moment, in order. The BLE client packs them into as
// few packets as the MTU allows and queues what the link cannot drain yet.
void midi_out_send(const midi_msg_t *msgs, int n);
