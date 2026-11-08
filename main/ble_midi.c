#include "ble_midi.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "midi_out.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"

static const char *TAG = "ble_midi";

void ble_store_config_init(void);   // NimBLE bond store (no public header)

#define DEVICE_NAME        "FM-1"
#define PREFERRED_MTU      128
#define PKT_MAX            (PREFERRED_MTU - 3)
#define TXQ_DEPTH          64
#define TX_TASK_PRIO       9          // just below the player
#define SEEN_MAX           8
#define BAD_MAX            4
#define BAD_HOLD_US        (30 * 1000 * 1000)   // skip a device without the MIDI service this long
#define PREFER_SAVED_US    (3 * 1000 * 1000)    // wait this long for the saved address first
#define UUID_ONLY_REPORTS  3          // a nameless MIDI device must be seen this often
#define RESCAN_DELAY_US    (1500 * 1000)
#define CONNECT_TIMEOUT_MS 10000
#define NVS_NS             "ble_midi"
#define NVS_KEY_ADDR       "last_addr"

// BLE MIDI service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 and MIDI I/O
// characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3, bytes little-endian
static const ble_uuid128_t k_svc_uuid = BLE_UUID128_INIT(
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);
static const ble_uuid128_t k_chr_uuid = BLE_UUID128_INIT(
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77);

#define ITVL_MIN           6          // 7.5 ms, in 1.25 ms units
#define ITVL_MAX           12         // 15 ms
#define STATS_US           (10 * 1000 * 1000)
#define LATE_US            10000      // a packet waiting longer than this counts as late

typedef struct {
    int64_t queued_us;
    uint8_t len;
    uint8_t data[PKT_MAX];
} pkt_t;

// A device seen while scanning. The name and the service UUID can arrive in
// separate reports (advertisement and scan response).
typedef struct {
    ble_addr_t addr;
    bool has_midi_uuid;
    uint8_t name_state;   // 0 no name yet, 1 "FM-1", 2 some other name
    uint8_t reports;
    char name[20];
} seen_t;

typedef struct {
    ble_addr_t addr;
    int64_t until_us;
} bad_t;

static ble_midi_state_cb_t s_cb;
static uint8_t s_own_addr_type;
static esp_timer_handle_t s_rescan_timer;
static QueueHandle_t s_txq;
static uint32_t s_tx_dropped;

// Link timing stats (sender task), logged every 10 s while connected
static uint32_t s_tx_pkts, s_tx_busy, s_tx_late;
static int64_t s_tx_wait_max_us, s_tx_wait_sum_us;
static atomic_int s_itvl;   // current connection interval, 1.25 ms units

// Host task only
static seen_t s_seen[SEEN_MAX];
static int s_nseen;
static bad_t s_bad[BAD_MAX];
static ble_addr_t s_saved;
static bool s_have_saved;
static int64_t s_scan_start_us;
static bool s_connecting;
static ble_addr_t s_peer;
static char s_peer_name[20];
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_cccd_handle;
static bool s_security_tried;

// Read by the sender task once s_ready is set
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_val_handle;
static atomic_bool s_ready;
static atomic_int s_mtu = BLE_ATT_MTU_DFLT;

static int gap_event(struct ble_gap_event *ev, void *arg);

static const char *addr_str(const ble_addr_t *a)
{
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
    return buf;
}

static void set_state(ble_midi_state_t st)
{
    if (s_cb) {
        s_cb(st, st == BLE_MIDI_CONNECTED ? s_peer_name : NULL);
    }
}

// ---- Last connected address (NVS) ----

static void load_saved(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_saved);
        s_have_saved = nvs_get_blob(h, NVS_KEY_ADDR, &s_saved, &len) == ESP_OK && len == sizeof(s_saved);
        nvs_close(h);
    }
}

static void save_peer(void)
{
    if (s_have_saved && ble_addr_cmp(&s_saved, &s_peer) == 0) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, NVS_KEY_ADDR, &s_peer, sizeof(s_peer)) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    s_saved = s_peer;
    s_have_saved = true;
}

// ---- Scanning ----

static void schedule_rescan(void)
{
    esp_timer_stop(s_rescan_timer);   // fine if not running
    esp_timer_start_once(s_rescan_timer, RESCAN_DELAY_US);
}

static void start_scan(void)
{
    s_nseen = 0;
    s_connecting = false;
    s_scan_start_us = esp_timer_get_time();
    // Active scan: the scan responses carry the name or UUID on many devices
    struct ble_gap_disc_params params = { .passive = 0, .filter_duplicates = 0 };
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "scan start failed (%d)", rc);
        schedule_rescan();
        return;
    }
    set_state(BLE_MIDI_SCANNING);
}

static void rescan_cb(void *arg)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        start_scan();
    }
}

static bool is_bad(const ble_addr_t *a)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < BAD_MAX; i++) {
        if (s_bad[i].until_us > now && ble_addr_cmp(&s_bad[i].addr, a) == 0) {
            return true;
        }
    }
    return false;
}

static void mark_bad(const ble_addr_t *a)
{
    int slot = 0;
    for (int i = 1; i < BAD_MAX; i++) {
        if (s_bad[i].until_us < s_bad[slot].until_us) {
            slot = i;
        }
    }
    s_bad[slot].addr = *a;
    s_bad[slot].until_us = esp_timer_get_time() + BAD_HOLD_US;
}

static seen_t *seen_entry(const ble_addr_t *a)
{
    for (int i = 0; i < s_nseen; i++) {
        if (ble_addr_cmp(&s_seen[i].addr, a) == 0) {
            return &s_seen[i];
        }
    }
    if (s_nseen == SEEN_MAX) {
        s_nseen = 0;   // busy airwaves: start over
    }
    seen_t *e = &s_seen[s_nseen++];
    memset(e, 0, sizeof(*e));
    e->addr = *a;
    return e;
}

static void connect_to(const seen_t *e)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "scan cancel failed (%d)", rc);
    }
    s_connecting = true;
    s_peer = e->addr;
    snprintf(s_peer_name, sizeof(s_peer_name), "%s", e->name[0] ? e->name : DEVICE_NAME);

    // 7.5 to 15 ms connection interval, 4 s supervision timeout
    const struct ble_gap_conn_params cp = {
        .scan_itvl = 0x10,
        .scan_window = 0x10,
        .itvl_min = ITVL_MIN,
        .itvl_max = ITVL_MAX,
        .latency = 0,
        .supervision_timeout = 400,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    ESP_LOGI(TAG, "connecting to %s (%s)", s_peer_name, addr_str(&s_peer));
    set_state(BLE_MIDI_CONNECTING);
    rc = ble_gap_connect(s_own_addr_type, &s_peer, CONNECT_TIMEOUT_MS, &cp, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "connect failed (%d)", rc);
        s_connecting = false;
        set_state(BLE_MIDI_DISCONNECTED);
        schedule_rescan();
    }
}

static void on_disc(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0 || is_bad(&d->addr)) {
        return;
    }
    seen_t *e = seen_entry(&d->addr);
    if (e->reports < 255) {
        e->reports++;
    }
    for (int i = 0; i < f.num_uuids128; i++) {
        if (ble_uuid_cmp(&f.uuids128[i].u, &k_svc_uuid.u) == 0) {
            e->has_midi_uuid = true;
        }
    }
    if (f.name && f.name_len) {
        size_t n = f.name_len < sizeof(e->name) - 1 ? f.name_len : sizeof(e->name) - 1;
        memcpy(e->name, f.name, n);
        e->name[n] = '\0';
        e->name_state = strcmp(e->name, DEVICE_NAME) == 0 ? 1 : 2;
    }

    // Named "FM-1", or a nameless device advertising the MIDI service. A MIDI
    // device with another name (a phone, another synth) is never taken.
    bool match = e->name_state == 1 ||
                 (e->has_midi_uuid && e->name_state == 0 && e->reports >= UUID_ONLY_REPORTS);
    if (!match) {
        return;
    }
    // Prefer the last connected FM-1 for a few seconds after the scan starts
    bool saved = s_have_saved && ble_addr_cmp(&s_saved, &d->addr) == 0;
    if (!saved && s_have_saved && esp_timer_get_time() - s_scan_start_us < PREFER_SAVED_US) {
        return;
    }
    ESP_LOGI(TAG, "found %s (%s)%s%s, rssi %d", e->name[0] ? e->name : "(no name)", addr_str(&d->addr),
             e->has_midi_uuid ? ", MIDI service" : "", saved ? ", saved" : "", d->rssi);
    connect_to(e);
}

// ---- Connection setup: MTU -> service -> characteristic -> CCCD -> ready ----

static void drop(const char *what, int rc, bool not_midi)
{
    ESP_LOGE(TAG, "%s (%d), dropping %s", what, rc, addr_str(&s_peer));
    if (not_midi) {
        mark_bad(&s_peer);
    }
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);   // DISCONNECT follows and rescans
}

static void send_all_off(void)
{
    midi_msg_t m[48];
    int n = 0;
    for (int ch = 0; ch < 16; ch++) {
        static const uint8_t k_cc[] = { 120, 121, 123 };
        for (int i = 0; i < 3; i++) {
            m[n++] = (midi_msg_t){ .len = 3, .b = { (uint8_t)(0xB0 | ch), k_cc[i], 0 } };
        }
    }
    midi_out_send(m, n);
}

static void link_ready(void)
{
    save_peer();
    s_connecting = false;
    atomic_store(&s_ready, true);
    ESP_LOGI(TAG, "MIDI ready: %s (%s), MTU %d", s_peer_name, addr_str(&s_peer), atomic_load(&s_mtu));
    send_all_off();   // the all-off sent at a drop never reached the synth
    set_state(BLE_MIDI_CONNECTED);
}

static int on_subscribed(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status == 0) {
        link_ready();
        return 0;
    }
    // Some peripherals want an encrypted link first: pair (Just Works), then retry
    if (!s_security_tried && (err->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
                              err->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC))) {
        s_security_tried = true;
        ESP_LOGI(TAG, "synth asks for encryption, pairing");
        int rc = ble_gap_security_initiate(conn);
        if (rc != 0) {
            drop("pairing start failed", rc, false);
        }
        return 0;
    }
    // Writes to the MIDI characteristic can still work without notifications
    ESP_LOGW(TAG, "subscribe failed (%d), continuing", err->status);
    link_ready();
    return 0;
}

static void subscribe(void)
{
    static const uint8_t k_on[2] = { 0x01, 0x00 };
    int rc = ble_gattc_write_flat(s_conn, s_cccd_handle, k_on, sizeof(k_on), on_subscribed, NULL);
    if (rc != 0) {
        drop("subscribe failed", rc, false);
    }
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    if (err->status == 0) {
        if (!s_cccd_handle && ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            s_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE) {
        drop("descriptor discovery failed", err->status, false);
    } else if (s_cccd_handle) {
        subscribe();
    } else {
        link_ready();
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0) {
        s_val_handle = chr->val_handle;
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !s_val_handle) {
        drop("no MIDI I/O characteristic", err->status, true);
        return 0;
    }
    int rc = ble_gattc_disc_all_dscs(conn, s_val_handle, s_svc_end, on_dsc, NULL);
    if (rc != 0) {
        drop("descriptor discovery failed", rc, false);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !s_svc_start) {
        drop("no BLE MIDI service", err->status, true);
        return 0;
    }
    int rc = ble_gattc_disc_chrs_by_uuid(conn, s_svc_start, s_svc_end, &k_chr_uuid.u, on_chr, NULL);
    if (rc != 0) {
        drop("characteristic discovery failed", rc, false);
    }
    return 0;
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    if (err->status == 0) {
        atomic_store(&s_mtu, mtu);
    }
    // Carry on either way; the default MTU still fits a few messages per packet
    int rc = ble_gattc_disc_svc_by_uuid(conn, &k_svc_uuid.u, on_svc, NULL);
    if (rc != 0) {
        drop("service discovery failed", rc, false);
    }
    return 0;
}

static void link_down(void)
{
    atomic_store(&s_ready, false);
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_connecting = false;
    s_val_handle = 0;
    s_svc_start = s_svc_end = 0;
    s_cccd_handle = 0;
    atomic_store(&s_mtu, BLE_ATT_MTU_DFLT);
    xQueueReset(s_txq);
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        if (!s_connecting) {
            on_disc(&ev->disc);
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!s_connecting && s_conn == BLE_HS_CONN_HANDLE_NONE) {
            schedule_rescan();
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            ESP_LOGW(TAG, "connection failed (%d)", ev->connect.status);
            s_connecting = false;
            set_state(BLE_MIDI_DISCONNECTED);
            schedule_rescan();
            return 0;
        }
        s_conn = ev->connect.conn_handle;
        s_security_tried = false;
        s_val_handle = s_cccd_handle = 0;
        s_svc_start = s_svc_end = 0;
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(s_conn, &desc) == 0) {
                atomic_store(&s_itvl, desc.conn_itvl);
                ESP_LOGI(TAG, "connected, interval %d.%02d ms", desc.conn_itvl * 5 / 4, (desc.conn_itvl * 125) % 100);
            }
        }
        ble_gattc_exchange_mtu(s_conn, on_mtu, NULL);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected (reason 0x%x)", ev->disconnect.reason);
        link_down();
        set_state(BLE_MIDI_DISCONNECTED);
        schedule_rescan();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ev->conn_update.status == 0 && ble_gap_conn_find(ev->conn_update.conn_handle, &desc) == 0) {
            atomic_store(&s_itvl, desc.conn_itvl);
            ESP_LOGI(TAG, "connection interval now %d.%02d ms", desc.conn_itvl * 5 / 4, (desc.conn_itvl * 125) % 100);
        }
        return 0;
    }

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
    case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
        // The synth asks for other connection timing. Accept, but inside its
        // range keep the interval at 15 ms or less: every interval step is
        // MIDI timing jitter. The LL request has self_params for the reply;
        // the L2CAP request has none (NULL) and NimBLE runs the update with
        // the very struct peer_params points to (ble_l2cap_sig.c, IDF 5.5), so
        // narrowing it there takes effect. Worst case it is ignored.
        struct ble_gap_upd_params *p = ev->type == BLE_GAP_EVENT_CONN_UPDATE_REQ
                                           ? ev->conn_update_req.self_params
                                           : (struct ble_gap_upd_params *)ev->conn_update_req.peer_params;
        ESP_LOGI(TAG, "synth asks for interval %d..%d (x1.25 ms), latency %d", p->itvl_min, p->itvl_max, p->latency);
        if (p->itvl_min <= ITVL_MAX) {
            if (p->itvl_max > ITVL_MAX) {
                p->itvl_max = ITVL_MAX;
            }
            if (p->itvl_min < ITVL_MIN) {
                p->itvl_min = ITVL_MIN;
            }
            p->latency = 0;
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        atomic_store(&s_mtu, ev->mtu.value);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) {
            ESP_LOGI(TAG, "link encrypted");
            if (!atomic_load(&s_ready) && s_cccd_handle) {
                subscribe();
            }
        } else if (!atomic_load(&s_ready)) {
            drop("pairing failed", ev->enc_change.status, false);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The synth forgot our bond: delete ours and pair again
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        return 0;   // MIDI from the synth: not used

    default:
        return 0;
    }
}

// ---- Host task ----

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable BLE address (%d)", rc);
        return;
    }
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset (reason %d)", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---- MIDI out ----

static void enqueue(pkt_t *p)
{
    p->queued_us = esp_timer_get_time();
    if (xQueueSend(s_txq, p, pdMS_TO_TICKS(20)) != pdTRUE) {
        if (s_tx_dropped++ % 50 == 0) {
            ESP_LOGW(TAG, "link busy, %lu packets dropped so far", (unsigned long)s_tx_dropped);
        }
    }
}

// BLE MIDI packet: header 0x80 | ts[12:7], then per message a timestamp byte
// 0x80 | ts[6:0] and the full message. ts is a 13-bit millisecond counter.
void midi_out_send(const midi_msg_t *msgs, int n)
{
    if (!atomic_load(&s_ready) || n <= 0) {
        return;
    }
    int limit = atomic_load(&s_mtu) - 3;
    if (limit > PKT_MAX) {
        limit = PKT_MAX;
    }
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint8_t header = (uint8_t)(0x80 | ((ms >> 7) & 0x3F));
    uint8_t stamp = (uint8_t)(0x80 | (ms & 0x7F));

    pkt_t p = { .len = 0 };
    for (int i = 0; i < n; i++) {
        int need = 1 + msgs[i].len;
        if (p.len > 0 && p.len + need > limit) {
            enqueue(&p);
            p.len = 0;
        }
        if (p.len == 0) {
            p.data[p.len++] = header;
        }
        p.data[p.len++] = stamp;
        memcpy(&p.data[p.len], msgs[i].b, msgs[i].len);
        p.len += msgs[i].len;
    }
    if (p.len > 1) {
        enqueue(&p);
    }
}

static void log_stats(void)
{
    int itvl = atomic_load(&s_itvl);
    ESP_LOGI(TAG, "link: interval %d.%02d ms, %lu packets, wait avg %lld us max %lld us, >10 ms %lu, busy %lu, dropped %lu",
             itvl * 5 / 4, (itvl * 125) % 100, (unsigned long)s_tx_pkts,
             s_tx_pkts ? s_tx_wait_sum_us / s_tx_pkts : 0, s_tx_wait_max_us, (unsigned long)s_tx_late,
             (unsigned long)s_tx_busy, (unsigned long)s_tx_dropped);
    s_tx_pkts = s_tx_busy = s_tx_late = 0;
    s_tx_wait_max_us = s_tx_wait_sum_us = 0;
}

// Writes queued packets without response. When the controller has no room
// (ENOMEM / EBUSY), waits a tick so the link drains instead of piling up.
// The wait from queueing to a successful write is the jitter this side adds.
static void tx_task(void *arg)
{
    pkt_t p;
    int64_t next_stats = esp_timer_get_time() + STATS_US;
    for (;;) {
        if (xQueueReceive(s_txq, &p, pdMS_TO_TICKS(1000)) == pdTRUE) {
            for (int tries = 0; atomic_load(&s_ready) && tries < 100; tries++) {
                int rc = ble_gattc_write_no_rsp_flat(s_conn, s_val_handle, p.data, p.len);
                if (rc == 0) {
                    int64_t wait = esp_timer_get_time() - p.queued_us;
                    s_tx_pkts++;
                    s_tx_wait_sum_us += wait;
                    if (wait > s_tx_wait_max_us) {
                        s_tx_wait_max_us = wait;
                    }
                    if (wait > LATE_US) {
                        s_tx_late++;
                    }
                    break;
                }
                if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) {
                    ESP_LOGW(TAG, "write failed (%d)", rc);
                    break;
                }
                s_tx_busy++;
                vTaskDelay(1);
            }
        }
        int64_t now = esp_timer_get_time();
        if (now >= next_stats) {
            next_stats = now + STATS_US;
            if (atomic_load(&s_ready) && s_tx_pkts > 0) {
                log_stats();
            }
        }
    }
}

// ---- Public ----

esp_err_t ble_midi_start(ble_midi_state_cb_t cb)
{
    s_cb = cb;
    load_saved();

    s_txq = xQueueCreate(TXQ_DEPTH, sizeof(pkt_t));
    ESP_RETURN_ON_FALSE(s_txq, ESP_ERR_NO_MEM, TAG, "queue");
    const esp_timer_create_args_t targs = {
        .callback = rescan_cb,
        .name = "ble_rescan",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_rescan_timer), TAG, "timer");
    ESP_RETURN_ON_FALSE(xTaskCreate(tx_task, "ble_tx", 3072, NULL, TX_TASK_PRIO, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "tx task");

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // If the synth asks for pairing: Just Works, bonded
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_att_set_preferred_mtu(PREFERRED_MTU);

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "started%s", s_have_saved ? ", preferring the saved FM-1 address" : "");
    return ESP_OK;
}

bool ble_midi_is_connected(void)
{
    return atomic_load(&s_ready);
}
