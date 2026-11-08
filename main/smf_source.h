#pragma once

// A MIDI file as a player source: loads and parses it into PSRAM (smf.h).

#include "esp_err.h"
#include "player.h"

// Errors as smf_load(): ESP_ERR_NOT_SUPPORTED / ESP_ERR_INVALID_RESPONSE mean
// "unsupported file". On success the player frees it through src->release.
esp_err_t smf_source_create(const char *path, player_source_t *src);
