#pragma once

// Song storage: FAT with wear levelling on the "storage" partition, mounted at
// /songs. Until the Wi-Fi upload page exists, songs come from the project's
// songs/ folder, flashed with `idf.py storage-flash`.

#include "esp_err.h"

#define SONGS_DIR "/songs"

esp_err_t songs_mount(void);

// File names of the MIDI files (.mid / .midi) in /songs, sorted. Returns the
// count; *names is NULL when there are none. Free with songs_free().
int songs_scan(char ***names);

void songs_free(char **names, int count);
