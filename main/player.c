#include "player.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "midi_mon.h"
#include "midi_out.h"

static const char *TAG = "player";

#define PERIOD_US     1000
#define LOOKAHEAD_Q   1        // pull events up to one quarter note ahead
#define BUF_EVENTS    256
#define BATCH_MAX     32
#define STOP_EXTRA    32
#define DRUM_CH       9
#define NOTE_NONE     0xFF
#define TASK_PRIO     10
#define TASK_STACK    6144
#define TASK_CORE     1

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static esp_timer_handle_t s_timer;
static player_end_cb_t s_on_end;

// Guarded by s_lock
static player_source_t s_src;
static bool s_ended;
static uint32_t s_tempo_us;
static int64_t s_last_us;
static uint64_t s_pos_q;               // song position in ticks, 16.16 fixed point
static mev_t s_buf[BUF_EVENTS];
static int s_rd, s_cnt;
static uint8_t s_sent[16][128];        // note number actually sent for each original note
static midi_msg_t s_batch[BATCH_MAX];
static int s_nbatch;

static atomic_bool s_playing;
static atomic_int s_tempo_pct = 100;
static atomic_int s_transpose;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void flush(void)
{
    if (s_nbatch > 0) {
        midi_mon_feed(s_batch, s_nbatch);
        midi_out_send(s_batch, s_nbatch);
        s_nbatch = 0;
    }
}

static void push(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (s_nbatch == BATCH_MAX) {
        flush();
    }
    midi_msg_t *m = &s_batch[s_nbatch++];
    m->len = (uint8_t)midi_msg_len(status);
    m->b[0] = status;
    m->b[1] = d1;
    m->b[2] = d2;
}

// Transpose applies to note numbers at send time, except on the drum channel.
// A held note is released with the number it was sent with, so a transpose
// change during playback never leaves a note hanging.
static void send_event(const mev_t *e)
{
    uint8_t type = e->status & 0xF0;
    uint8_t ch = e->status & 0x0F;
    uint8_t key = e->d1 & 0x7F;

    if (type == 0x90 && e->d2 > 0) {
        int n = key;
        if (ch != DRUM_CH) {
            n = clampi(key + atomic_load(&s_transpose), 0, 127);
        }
        uint8_t old = s_sent[ch][key];
        if (old != NOTE_NONE && old != n) {
            push(0x80 | ch, old, 0);   // same note still held from before a transpose change
        }
        s_sent[ch][key] = (uint8_t)n;
        push(e->status, (uint8_t)n, e->d2);
    } else if (type == 0x80 || type == 0x90) {
        uint8_t n = s_sent[ch][key];
        if (n != NOTE_NONE) {           // drop offs for notes that were never sent
            s_sent[ch][key] = NOTE_NONE;
            push(e->status, n, e->d2);
        }
    } else if (type == 0xA0) {
        uint8_t n = s_sent[ch][key];
        if (n != NOTE_NONE) {
            push(e->status, n, e->d2);
        }
    } else {
        push(e->status, e->d1, e->d2);
    }
}

static void all_off(void)
{
    for (int ch = 0; ch < 16; ch++) {
        push(0xB0 | ch, 120, 0);   // All Sound Off
        push(0xB0 | ch, 121, 0);   // Reset All Controllers (sustain, pitch bend, expression)
        push(0xB0 | ch, 123, 0);   // All Notes Off
    }
    if (s_src.stop_msgs) {
        midi_msg_t extra[STOP_EXTRA];
        int n = s_src.stop_msgs(s_src.ctx, extra, STOP_EXTRA);
        for (int i = 0; i < n; i++) {
            push(extra[i].b[0], extra[i].b[1], extra[i].b[2]);
        }
    }
    flush();
    memset(s_sent, NOTE_NONE, sizeof(s_sent));
}

static void stop_locked(void)
{
    if (!atomic_load(&s_playing)) {
        return;
    }
    esp_timer_stop(s_timer);
    all_off();
    midi_mon_stop();
    atomic_store(&s_playing, false);
    if (s_src.release) {
        s_src.release(s_src.ctx);
    }
    memset(&s_src, 0, sizeof(s_src));
}

static void step(void)
{
    int64_t now = esp_timer_get_time();
    int64_t dt = now - s_last_us;
    s_last_us = now;

    // ticks = us * ppqn / tempo_us, scaled by the tempo control
    int pct = atomic_load(&s_tempo_pct);
    s_pos_q += (uint64_t)(dt * s_src.ppqn * pct * 65536 / ((int64_t)s_tempo_us * 100));
    uint32_t pos = (uint32_t)(s_pos_q >> 16);

    for (;;) {
        if (s_rd == s_cnt) {
            if (s_ended) {
                break;
            }
            s_cnt = s_src.pull(s_src.ctx, pos + (uint32_t)s_src.ppqn * LOOKAHEAD_Q, s_buf, BUF_EVENTS, &s_ended);
            s_rd = 0;
            if (s_cnt <= 0) {
                s_cnt = 0;
                break;
            }
        }
        const mev_t *e = &s_buf[s_rd];
        if (e->tick > pos) {
            break;
        }
        s_rd++;
        if (e->status == MEV_TEMPO) {
            uint32_t us = (uint32_t)e->d1 << 16 | (uint32_t)e->d2 << 8 | e->d3;
            if (us > 0) {
                s_tempo_us = us;
            }
        } else {
            send_event(e);
        }
    }
    flush();
}

static void timer_cb(void *arg)
{
    xTaskNotifyGive(s_task);
}

static void player_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool ended = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (atomic_load(&s_playing)) {
            step();
            if (s_ended && s_rd == s_cnt) {
                stop_locked();
                ended = true;
            }
        }
        xSemaphoreGive(s_lock);
        if (ended && s_on_end) {
            s_on_end();
        }
    }
}

esp_err_t player_init(player_end_cb_t on_end)
{
    s_on_end = on_end;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    BaseType_t ok = xTaskCreatePinnedToCore(player_task, "player", TASK_STACK, NULL, TASK_PRIO, &s_task, TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .name = "player",
    };
    return esp_timer_create(&args, &s_timer);
}

esp_err_t player_start(const player_source_t *src)
{
    ESP_RETURN_ON_FALSE(src && src->pull && src->ppqn && src->tempo_us, ESP_ERR_INVALID_ARG, TAG, "bad source");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    stop_locked();
    s_src = *src;
    s_tempo_us = src->tempo_us;
    s_pos_q = 0;
    s_rd = s_cnt = 0;
    s_ended = false;
    s_nbatch = 0;
    memset(s_sent, NOTE_NONE, sizeof(s_sent));
    midi_mon_start();
    s_last_us = esp_timer_get_time();
    atomic_store(&s_playing, true);
    esp_err_t err = esp_timer_start_periodic(s_timer, PERIOD_US);
    if (err != ESP_OK) {
        stop_locked();
    }
    xSemaphoreGive(s_lock);
    return err;
}

void player_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stop_locked();
    xSemaphoreGive(s_lock);
}

bool player_is_playing(void)
{
    return atomic_load(&s_playing);
}

void player_set_tempo_scale(int percent)
{
    atomic_store(&s_tempo_pct, clampi(percent, 50, 150));
}

void player_set_transpose(int semitones)
{
    atomic_store(&s_transpose, clampi(semitones, -12, 12));
}
