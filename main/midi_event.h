#pragma once

// Timed MIDI events shared by the event sources (SMF parser, lo-fi generator),
// the player and the MIDI output. Plain C, no ESP-IDF types.

#include <stdint.h>

// One timed channel message, or a tempo change
typedef struct {
    uint32_t tick;     // absolute, in the source's PPQN
    uint8_t status;    // 0x80..0xEF channel message, or MEV_TEMPO
    uint8_t d1;
    uint8_t d2;
    uint8_t d3;        // MEV_TEMPO only: us per quarter note = d1 << 16 | d2 << 8 | d3
} mev_t;

#define MEV_TEMPO 0xFF

// One complete channel message as it goes out (always with its status byte)
typedef struct {
    uint8_t len;
    uint8_t b[3];
} midi_msg_t;

static inline int midi_msg_len(uint8_t status)
{
    uint8_t type = status & 0xF0;
    return (type == 0xC0 || type == 0xD0) ? 2 : 3;
}
