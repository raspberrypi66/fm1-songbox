#include "midi_mon.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "midi_mon";

#define REPORT_US   (10 * 1000 * 1000)
#define LONG_NOTE_MS 10000
#define BEND_CENTER 8192

static bool s_on[16][128];
static uint32_t s_on_ms[16][128];
static int s_sounding, s_peak;
static uint32_t s_msgs, s_notes, s_overlaps, s_stray_offs;
static uint16_t s_bend[16];
static uint8_t s_cc11[16];
static int64_t s_next_report;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void all_released(void)
{
    memset(s_on, 0, sizeof(s_on));
    s_sounding = 0;
}

static int long_held(void)
{
    uint32_t now = now_ms();
    int n = 0;
    for (int ch = 0; ch < 16; ch++) {
        for (int k = 0; k < 128; k++) {
            if (s_on[ch][k] && now - s_on_ms[ch][k] > LONG_NOTE_MS) {
                n++;
            }
        }
    }
    return n;
}

static void report(const char *when)
{
    ESP_LOGI(TAG, "%s: %lu msgs, %lu notes, sounding %d (peak %d), overlaps %lu, stray offs %lu, held >10 s %d",
             when, (unsigned long)s_msgs, (unsigned long)s_notes, s_sounding, s_peak,
             (unsigned long)s_overlaps, (unsigned long)s_stray_offs, long_held());
}

void midi_mon_start(void)
{
    all_released();
    s_peak = 0;
    s_msgs = s_notes = s_overlaps = s_stray_offs = 0;
    for (int ch = 0; ch < 16; ch++) {
        s_bend[ch] = BEND_CENTER;
        s_cc11[ch] = 127;
    }
    s_next_report = esp_timer_get_time() + REPORT_US;
}

static void note_off(int ch, int key)
{
    if (s_on[ch][key]) {
        s_on[ch][key] = false;
        s_sounding--;
    } else {
        s_stray_offs++;
    }
}

void midi_mon_feed(const midi_msg_t *msgs, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *b = msgs[i].b;
        int type = b[0] & 0xF0;
        int ch = b[0] & 0x0F;
        s_msgs++;

        if (type == 0x90 && b[2] > 0) {
            s_notes++;
            if (s_on[ch][b[1]]) {
                s_overlaps++;   // note-on while the same note still sounds
            } else {
                s_on[ch][b[1]] = true;
                s_sounding++;
                if (s_sounding > s_peak) {
                    s_peak = s_sounding;
                }
            }
            s_on_ms[ch][b[1]] = now_ms();
        } else if (type == 0x80 || type == 0x90) {
            note_off(ch, b[1]);
        } else if (type == 0xB0) {
            if (b[1] == 11) {
                s_cc11[ch] = b[2];
            } else if (b[1] == 121) {
                s_bend[ch] = BEND_CENTER;   // Reset All Controllers
                s_cc11[ch] = 127;
            } else if (b[1] == 120 || b[1] == 123) {
                for (int k = 0; k < 128; k++) {
                    if (s_on[ch][k]) {
                        s_on[ch][k] = false;
                        s_sounding--;
                    }
                }
            }
        } else if (type == 0xE0) {
            s_bend[ch] = (uint16_t)(b[1] | (b[2] << 7));
        }
    }

    int64_t now = esp_timer_get_time();
    if (now >= s_next_report) {
        s_next_report = now + REPORT_US;
        report("playing");
    }
}

void midi_mon_stop(void)
{
    report("stopped");
    int off_center = 0, cc11_low = 0;
    for (int ch = 0; ch < 16; ch++) {
        off_center += s_bend[ch] != BEND_CENTER;
        cc11_low += s_cc11[ch] != 127;
    }
    ESP_LOGI(TAG, "end state: notes sounding %d, pitch bend off center on %d channels, CC11 below 127 on %d channels -> %s",
             s_sounding, off_center, cc11_low, (s_sounding == 0 && !off_center && !cc11_low) ? "OK" : "CHECK");
}
