#pragma once

// Checks the outgoing MIDI stream while playing and logs it over serial:
// notes sounding and their peak, same-pitch overlaps, stray note-offs, notes
// held over 10 s, and the pitch bend / CC11 state left after stop.
// Called from the player task only.

#include "midi_event.h"

void midi_mon_start(void);
void midi_mon_feed(const midi_msg_t *msgs, int n);
// Call after the stop messages went out: logs the summary and the end state
void midi_mon_stop(void);
