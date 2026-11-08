#include "songs.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "songs";

static wl_handle_t s_wl = WL_INVALID_HANDLE;

esp_err_t songs_mount(void)
{
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,   // never flashed: start with an empty song store
        .max_files = 4,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_fat_spiflash_mount_rw_wl(SONGS_DIR, "storage", &cfg, &s_wl), TAG, "mount");

    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(SONGS_DIR, &total, &free_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "%s mounted: %llu KB free of %llu KB", SONGS_DIR, free_bytes / 1024, total / 1024);
    }
    return ESP_OK;
}

static bool is_midi(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".mid") == 0 || strcasecmp(dot, ".midi") == 0);
}

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

int songs_scan(char ***out)
{
    *out = NULL;
    DIR *dir = opendir(SONGS_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", SONGS_DIR);
        return 0;
    }
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_type == DT_DIR || !is_midi(e->d_name)) {
            continue;
        }
        if (n == cap) {
            int grown_cap = cap ? cap * 2 : 32;
            char **grown = realloc(names, (size_t)grown_cap * sizeof(char *));
            if (!grown) {
                break;
            }
            names = grown;
            cap = grown_cap;
        }
        names[n] = strdup(e->d_name);
        if (!names[n]) {
            break;
        }
        n++;
    }
    closedir(dir);

    if (n > 1) {
        qsort(names, (size_t)n, sizeof(char *), cmp_name);
    }
    ESP_LOGI(TAG, "%d songs", n);
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "  %s", names[i]);
    }
    *out = names;
    return n;
}

void songs_free(char **names, int count)
{
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}
