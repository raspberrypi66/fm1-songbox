#pragma once

// The lo-fi composer as a player source. Logs each new piece and section
// and, at stop, the generator's checks (peak notes, shortened and dropped
// notes, section types seen).

#include <stdint.h>

#include "esp_err.h"
#include "player.h"

// Fills `src`; the player frees it through src->release. first_piece is the
// number (0..LOFI_PIECES-1) of the first piece in the chain. on_piece (may be
// NULL) is called from the player task, with the player's lock held, when a
// new piece's first bar has been generated (the first piece included): store
// the number and return.
esp_err_t lofi_source_create(uint32_t first_piece, void (*on_piece)(uint32_t piece), player_source_t *src);
