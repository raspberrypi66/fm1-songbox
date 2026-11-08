#pragma once

#include "lvgl.h"

// The app: owns the state (selection, tempo, transpose, BLE link) and wires
// the screen UI to the player, the songs in /songs, the lo-fi generator and
// the BLE MIDI client.
void app_start(lv_display_t *disp);
