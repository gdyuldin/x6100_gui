/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog_ft8.h"

#include "ft8/worker.h"
#include "ft8/qso.h"
#include "ft8/utils.h"
#include "lvgl/lvgl.h"
#include "dialog.h"
#include "styles.h"
#include "params/params.h"
#include "cfg/digital_modes.h"
#include "radio.h"
#include "audio.h"
#include "keyboard.h"
#include "events.h"
#include "buttons.h"
#include "main_screen.h"
#include "qth/qth.h"
#include "msg.h"
#include "util.h"
#include "recorder.h"
#include "tx_info.h"
#include "textarea_window.h"
#include "dsp.h"

#include "widgets/lv_waterfall.h"
#include "widgets/lv_finder.h"

#include <ft8lib/message.h>
#include "ft8/worker.h"
#include "adif.h"
#include "qso_log.h"
#include "scheduler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#define DECIM           6
#define SAMPLE_RATE     (AUDIO_CAPTURE_RATE / DECIM)

#define WIDTH           771

#define UNKNOWN_SNR     99

#define MAX_PWR         10.0f

#define FT8_WIDTH_HZ    50
#define FT4_WIDTH_HZ    83

#define MAX_TABLE_MSG   512
#define CLEAN_N_ROWS    64

/* Max unseen candidates to keep per slot to avoid unbounded memory growth */
#define MAX_UNSEEN 100

/*
 * FT8 late-start TX:
 * - Allow starting TX in the current slot up to 5 seconds.
 * - If TX starts late, crop the beginning so the burst ends at 14.5s.
 *
 * Keep FT4 behavior unchanged.
 */
#define MAX_TX_START_DELAY 5.0f
#define MAX_TX_START_DELAY_FT4 1.5f
#define FT8_TX_END_SEC 14.5f

#define WAIT_SYNC_TEXT "Wait sync"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef enum {
    RX_PROCESS,
    TX_PROCESS,
} ft8_state_t;

/* ft8_cell_type_t / cell_data_t / table_view_* live in ft8/table_view.h.
 * slot_info_t / ft8_log_* live in ft8/ft8_log.h. */
#include "ft8/table_view.h"
#include "ft8/ft8_log.h"

static bool get_time_slot(struct timespec now, float *sec_since_start);

static time_t ft8_get_current_slot_start(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    float sec_since_slot_start = 0.0f;
    (void)get_time_slot(now, &sec_since_slot_start);

    double now_f = (double)now.tv_sec + (double)now.tv_nsec / 1.0e9;
    double slot_start_f = now_f - (double)sec_since_slot_start;
    return (time_t)slot_start_f;
}

static ft8_state_t state = RX_PROCESS;
static Subject    *tx_enabled;
static Subject    *cq_enabled;
static bool        tx_time_slot;
static bool        cq_paused_for_qso = false;
/* AutoSel recovery mode: if a non-CQ QSO runs out of TX repeats and leaves a
 * current QSO active (no timeout), allow AutoSel to start a new QSO by
 * resetting the old one only after a new candidate is picked.
 */
static bool        autosel_recover_mode = false;

/* After TX repeats are exhausted in a QSO, we keep the current QSO (no timeout),
 * but allow higher-priority actions (reply to someone calling us, AutoSel).
 */
static bool        qso_tx_exhausted = false;

/* Best-effort tracking of current QSO remote callsign (used to detect "other caller"). */
static char        qso_active_call[16] = {0};

/* True when current QSO was started by AutoSel (not manual click / not to-me). */
static bool        autosel_qso_active = false;

/* Number of times we've actually transmitted the current grid-exchange message
 * (remote local GRID) in this AutoSel QSO.
 */
static uint8_t     autosel_grid_tx_count = 0;

/* Pending "to-me" caller observed while TX is exhausted (priority reply). */
static bool         pending_to_me_valid = false;
static bool         pending_to_me_odd = false;
static ftx_msg_meta_t pending_to_me_meta;

/* Pending "to-me GRID" caller observed while AutoSel is trying to call a CQ
 * and repeatedly sending our grid. Used to switch before repeats are exhausted.
 */
static bool          pending_grid_to_me_valid = false;
static bool          pending_grid_to_me_odd = false;
static ftx_msg_meta_t pending_grid_to_me_meta;

static ftx_tx_msg_t tx_msg;

/* Helper that returns the LVGL table object owned by table_view; used by the
 * fade animation and a few legacy spots. */
#define table  (table_view_obj())

static lv_timer_t *timer = NULL;
static lv_anim_t   fade;
static bool        fade_run        = false;
static bool        disable_buttons = false;

static lv_obj_t *finder;
static lv_obj_t *waterfall;
static uint16_t  waterfall_nfft;
/* Waterfall marker: draw a red horizontal line at each new slot boundary. */
static uint64_t ft8_waterfall_slot_start_seen = 0;

/* Auto DNF: tuning parameters (file-loaded) + the module instance. */
#include "ft8/params.h"
#include "ft8/auto_dnf.h"
#include "ft8/audio_worker.h"

static ft8_tuning_t    ft8_tuning = {
    .scan_start_sec = FT8_AUTO_DNF_SCAN_START_SEC_DEFAULT,
    .scan_end_sec   = FT8_AUTO_DNF_SCAN_END_SEC_DEFAULT,
    .clear_time_sec = FT8_AUTO_DNF_CLEAR_TIME_SEC_DEFAULT,
    .min_delta_db   = FT8_AUTO_DNF_MIN_DELTA_DB_DEFAULT,
};
static auto_dnf_ctx_t *s_auto_dnf = NULL;
static audio_worker_t *s_audio_worker = NULL;


/* Cooperative sleep: wake up immediately when stop is requested instead of
 * sitting out a full usleep() that pthread_cancel would otherwise have cut
 * short at an unknown point. */



static adif_log         ft8_log;
static FTxQsoProcessor *qso_processor;

static double cur_lat, cur_lon;
/* Whether local QTH is valid and can be used for distance computations */
static bool local_qth_valid = false;

static int32_t filter_low, filter_high;

static float base_gain_offset;

static void construct_cb(lv_obj_t *parent);
static void key_cb(lv_event_t * e);
static void destruct_cb();
static void audio_cb(unsigned int n, float complex *samples);
static void rotary_cb(int32_t diff);

/* Audio worker callbacks (run on worker thread, never touch LVGL directly). */
static void on_message_cb(const char *text, int snr, float freq_hz, float time_sec,
                          const slot_info_t *info, void *ctx);
static void on_psd_cb(const float *psd, uint16_t nfft, float sec_since_slot_start,
                      const slot_info_t *info, void *ctx);
static void on_slot_end_cb(const slot_info_t *info, void *ctx);
static void on_tick_cb(const slot_info_t *info, bool new_slot,
                       float sec_since_slot_start, void *ctx);

/* Auto DNF API lives in ft8/auto_dnf.h. */

/* AutoSel: candidate list, blacklists and the selection mode all live in
 * ft8/auto_sel.{cpp,h}. The current mode is the only piece of UI state we
 * keep here. */
#include "ft8/auto_sel.h"

static auto_sel_mode_t auto_sel_mode = AUTO_SEL_OFF; /* default: off (user must opt-in) */

static bool is_grid_exchange_msg(const char *msg) {
    if (!msg || msg[0] == '\0') return false;
    char t1[16] = {0};
    char t2[16] = {0};
    char t3[16] = {0};
    if (sscanf(msg, "%15s %15s %15s", t1, t2, t3) != 3) return false;
    /* Grid exchange is exactly 3 tokens and last token is a Maidenhead grid. */
    return qth_grid_check(t3);
}


static const char * cq_all_label_getter();
static const char * protocol_label_getter();
static const char * tx_cq_label_getter();
static const char * tx_call_label_getter();
static const char * hold_freq_label_getter();
static const char * auto_label_getter();
static const char * auto_dnf_label_getter();
static const char * auto_sel_label_getter();
static const char * xit_label_getter();

static void show_cq_all_cb(struct button_item_t *btn);
static void mode_ft4_ft8_cb(struct button_item_t *btn);
static void tx_cq_en_dis_cb(struct button_item_t *btn);
static void tx_call_en_dis_cb(struct button_item_t *btn);

static void hold_tx_freq_cb(struct button_item_t *btn);
static void mode_auto_cb(struct button_item_t *btn);
static void auto_dnf_cb(struct button_item_t *btn);
static void cq_modifier_cb(struct button_item_t *btn);
static void time_sync(struct button_item_t *btn);
static void mode_auto_sel_cb(struct button_item_t *btn);
static void xit_cb(struct button_item_t *btn);

static void free_msg_cb(struct button_item_t *btn);

static void force_save_qso(struct button_item_t *btn);

static void on_table_press(const cell_data_t *cell_data);
static void on_table_close(void);
static void on_table_vol_change(int32_t delta);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close();

static void free_msg_open();
static void free_msg_close();
static bool free_msg_cancel_cb();
static bool free_msg_ok_cb();

static void add_info(const char * fmt, ...);
static void add_tx_text(const char * text);
static void add_rx_text(int16_t snr, const char * text, slot_info_t *s_info, float freq_hz, float time_sec);
static void tx_worker(float sec_since_slot_start);
static void make_cq_msg(const char *callsign, const char *qth, const char *cq_mod, char *text);
static bool get_time_slot(struct timespec now, float *time_since_start);

static void schedule_cq_tx_if_needed();



// button label is current state, press action and name - next state

static buttons_page_t btn_page_1;
static buttons_page_t btn_page_2;
static buttons_page_t btn_page_3;
static buttons_page_t btn_page_4;

static button_item_t button_page_1 = { .type=BTN_TEXT, .label = "(Page: 1:4)", .press = button_next_page_cb, .next=&btn_page_2};
static button_item_t button_show_cq_all = { .type=BTN_TEXT_FN, .label_fn = cq_all_label_getter, .press = show_cq_all_cb, .subj=&cfg.ft8_show_all.val};
static button_item_t button_mode_ft4_ft8 = { .type=BTN_TEXT_FN, .label_fn = protocol_label_getter, .press = mode_ft4_ft8_cb, .subj=&cfg.ft8_protocol.val };
static button_item_t button_tx_cq_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_cq_label_getter, .press = tx_cq_en_dis_cb };
static button_item_t button_tx_call_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_call_label_getter, .press = tx_call_en_dis_cb};

static button_item_t button_page_2 = { .type=BTN_TEXT, .label = "(Page: 2:4)", .press = button_next_page_cb, .next=&btn_page_3};
static button_item_t button_hold_freq = { .type=BTN_TEXT_FN, .label_fn = hold_freq_label_getter, .press = hold_tx_freq_cb, .subj=&cfg.ft8_hold_freq.val };
static button_item_t button_auto_en_dis = { .type=BTN_TEXT_FN, .label_fn = auto_label_getter, .press = mode_auto_cb, .subj=&cfg.ft8_auto.val };
static button_item_t button_force_save = { .type=BTN_TEXT, .label = "Force QSO\nsave", .press = force_save_qso };
static button_item_t button_free_msg = { .type=BTN_TEXT, .label = "Free\nMSG", .press = free_msg_cb };

static button_item_t button_page_3 = { .type=BTN_TEXT, .label = "(Page: 3:4)", .press = button_next_page_cb, .next=&btn_page_4};
static button_item_t button_cq_mod = { .type=BTN_TEXT, .label = "CQ\nModifier", .press = cq_modifier_cb };
static button_item_t button_time_sync = { .type=BTN_TEXT, .label = "Time\nSync", .press = time_sync };
static button_item_t button_auto_sel = { .type=BTN_TEXT_FN, .label_fn = auto_sel_label_getter, .press = mode_auto_sel_cb };
static button_item_t button_auto_dnf = { .type=BTN_TEXT_FN, .label_fn = auto_dnf_label_getter, .press = auto_dnf_cb, .subj=&cfg.ft8_auto_dnf.val };

static button_item_t button_page_4 = { .type=BTN_TEXT, .label = "(Page: 4:4)", .press = button_next_page_cb, .next=&btn_page_1};
static button_item_t button_xit = { .type=BTN_TEXT_FN, .label_fn = xit_label_getter, .press = xit_cb, .subj=&cfg.ft8_xit.val };

static buttons_page_t btn_page_1 = {
    {&button_page_1, &button_auto_sel, &button_mode_ft4_ft8, &button_tx_cq_en_dis, &button_tx_call_en_dis}
};

static buttons_page_t btn_page_2 = {
    {&button_page_2, &button_hold_freq, &button_auto_en_dis, &button_force_save, &button_free_msg}
};

static buttons_page_t btn_page_3 = {
    {&button_page_3, &button_cq_mod, &button_time_sync, &button_show_cq_all, &button_auto_dnf}
};

static buttons_page_t btn_page_4 = {
    {&button_page_4, &button_xit, NULL, NULL, NULL}
};

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = audio_cb,
    .rotary_cb = rotary_cb,
    .key_cb = key_cb,
};

dialog_t *dialog_ft8 = &dialog;

static void save_qso(const char *remote_callsign, const char *remote_grid, const int r_snr, const int s_snr) {
    time_t now = time(NULL);

    char * canonized_call = util_canonize_callsign(remote_callsign, false);
    qso_log_record_t qso = qso_log_record_create(
        params.callsign.x,
        canonized_call,
        now, subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
        s_snr, r_snr, subject_get_int(cfg_cur.fg_freq), NULL, NULL,
        params.qth.x, remote_grid
    );
    free(canonized_call);

    adif_add_qso(ft8_log, qso);

    // Save QSO to sqlite log
    qso_log_record_save(qso);

    if (strlen(remote_grid) >= 4 && qth_grid_check(remote_grid) && local_qth_valid) {
        double lat, lon, dist;
        qth_str_to_pos(remote_grid, &lat, &lon);
        dist = qth_pos_dist(lat, lon, cur_lat, cur_lon);
        msg_schedule_long_text_fmt("Saved QSO de %s %d %d (%.0f km)", remote_callsign, s_snr, r_snr, dist);
    } else {
        msg_schedule_long_text_fmt("Saved QSO de %s %d %d", remote_callsign, s_snr, r_snr);
    }

    lv_finder_clear_cursor(finder);
    /* Already-worked status is recorded in the qso log via qso_log_record_save(). */
    /* If we paused CQ to complete this QSO, restore it now */
    if (cq_paused_for_qso) {
        subject_set_int(cq_enabled, true);
        cq_paused_for_qso = false;
        schedule_cq_tx_if_needed();
    }
}

static void schedule_cq_tx_if_needed() {
    if (!cq_enabled || !subject_get_int(cq_enabled)) return;
    if (!tx_enabled || !subject_get_int(tx_enabled)) return;
    if (tx_msg.msg[0] != '\0') return;
    if (ftx_qso_processor_has_current(qso_processor)) return;

    if (strlen(params.callsign.x) == 0) return;

    make_cq_msg(params.callsign.x, params.qth.x, params.ft8_cq_modifier.x, tx_msg.msg);
    tx_msg.repeats = subject_get_int(cfg.ft8_max_repeats.val);
    tx_msg.force_free_text = false;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    float time_since_slot_start;
    tx_time_slot = !get_time_slot(now, &time_since_slot_start);
    float max_delay = (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8) ? MAX_TX_START_DELAY : MAX_TX_START_DELAY_FT4;
    if (time_since_slot_start < max_delay) {
        tx_time_slot = !tx_time_slot;
    }

    if (tx_msg.msg[2] == '_') {
        msg_schedule_text_fmt("Next TX: CQ %s", tx_msg.msg + 3);
    } else {
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
    }
}

static void worker_init() {
    qso_processor = ftx_qso_processor_init(params.callsign.x, params.qth.x, save_qso,
                                           subject_get_int(cfg.ft8_max_repeats.val));

    audio_worker_cb_t cb = {
        .on_message  = on_message_cb,
        .on_psd      = on_psd_cb,
        .on_slot_end = on_slot_end_cb,
        .on_tick     = on_tick_cb,
        .ctx         = NULL,
    };

    s_audio_worker = audio_worker_create(AUDIO_CAPTURE_RATE, DECIM,
                                         (ftx_protocol_t)subject_get_int(cfg.ft8_protocol.val),
                                         filter_low, filter_high, &cb);
    if (!s_audio_worker) {
        LV_LOG_ERROR("Failed to create audio_worker");
        ftx_qso_processor_delete(qso_processor);
        return;
    }
    waterfall_nfft = audio_worker_nfft(s_audio_worker);

    if (audio_worker_start(s_audio_worker) != 0) {
        LV_LOG_ERROR("Failed to start audio_worker thread");
        audio_worker_destroy(s_audio_worker);
        s_audio_worker = NULL;
        ftx_qso_processor_delete(qso_processor);
        return;
    }
}

static void worker_done() {
    state = RX_PROCESS;

    if (s_audio_worker) {
        audio_worker_stop(s_audio_worker);
        audio_worker_destroy(s_audio_worker);
        s_audio_worker = NULL;
    }

    radio_set_modem(false);
    ftx_qso_processor_delete(qso_processor);
    lv_finder_clear_cursor(finder);
    tx_msg.msg[0] = '\0';
    tx_msg.force_free_text = false;
    autosel_recover_mode = false;
    qso_tx_exhausted = false;
    pending_to_me_valid = false;
    qso_active_call[0] = '\0';

    /* Free any accumulated unseen candidates if the dialog stops mid-slot. */
    autosel_clear_unseen();
}

/* ---------- audio_worker callbacks (run on worker thread) --------------- */

typedef struct {
    float          *psd_copy;
    uint16_t        cnt;
    struct timespec ts;
    bool            add_marker;
} wf_frame_t;

static void wf_push_cb(void *arg) {
    wf_frame_t *f = (wf_frame_t *)arg;
    if (!f || !waterfall) return;
    if (f->add_marker) {
        lv_waterfall_add_marker_line(waterfall, lv_color_hex(0xFF0000));
    }
    lv_waterfall_add_data_with_ts(waterfall, f->psd_copy, f->cnt, f->ts);
    free(f->psd_copy);
}

static void on_psd_cb(const float *psd, uint16_t nfft, float sec_since_slot_start,
                      const slot_info_t *info, void *ctx) {
    (void)ctx;
    (void)sec_since_slot_start;
    uint32_t low_bin  = (uint32_t)(nfft / 2 + (int32_t)nfft * filter_low  / SAMPLE_RATE);
    uint32_t high_bin = (uint32_t)(nfft / 2 + (int32_t)nfft * filter_high / SAMPLE_RATE);
    if (high_bin > nfft) high_bin = nfft;
    if (high_bin <= low_bin) return;
    uint16_t cnt = (uint16_t)(high_bin - low_bin);

    /* Copy PSD band for waterfall (freed by wf_push_cb on UI thread). */
    wf_frame_t push = { .add_marker = false };
    push.cnt = cnt;
    push.psd_copy = (float *)malloc(cnt * sizeof(float));
    if (push.psd_copy) {
        memcpy(push.psd_copy, &psd[low_bin], cnt * sizeof(float));
    }
    push.ts.tv_sec  = info->slot_start;
    push.ts.tv_nsec = 0;

    /* Slot boundary marker. */
    int proto = subject_get_int(cfg.ft8_protocol.val);
    uint64_t slot_ns = (proto == FTX_PROTOCOL_FT4) ? 7500000000ULL : 15000000000ULL;
    uint64_t slot_id = (uint64_t)((uint64_t)info->slot_start * 1000000000ULL) / slot_ns;
    if (slot_id != ft8_waterfall_slot_start_seen) {
        ft8_waterfall_slot_start_seen = slot_id;
        push.add_marker = true;
    }

    /* Auto DNF: compute frame_ts as the wall-clock time of this PSD frame
     * (slot_start + sec_since_slot_start), not the bare slot boundary.
     * The PSD represents audio accumulated since the last spgramcf_reset,
     * which happened at approximately this wall-clock time. */
    if (s_auto_dnf && state == RX_PROCESS) {
        struct timespec frame_ts;
        double frame_time = (double)info->slot_start + (double)sec_since_slot_start;
        frame_ts.tv_sec  = (time_t)frame_time;
        frame_ts.tv_nsec = (long)((frame_time - (double)frame_ts.tv_sec) * 1.0e9);
        float dummy_sec;
        bool have_tx_msg    = tx_msg.msg[0] != '\0';
        bool is_our_tx_slot = (subject_get_int(tx_enabled) && have_tx_msg &&
                               (tx_time_slot == get_time_slot(frame_ts, &dummy_sec)));
        auto_dnf_on_psd(s_auto_dnf, psd, nfft, SAMPLE_RATE,
                        filter_low, filter_high, frame_ts, is_our_tx_slot);
    }

    scheduler_put(wf_push_cb, &push, sizeof(push));
}

static void on_message_cb(const char *text, int snr, float freq_hz, float time_sec,
                          const slot_info_t *info, void *ctx) {
    (void)ctx;
    add_rx_text((int16_t)snr, text, (slot_info_t *)info, freq_hz, time_sec);
}

static void on_slot_end_cb(const slot_info_t *info, void *ctx) {
    (void)ctx;
    state = RX_PROCESS;
    struct tm tm_buf;
    time_t slot_start_t = (time_t)info->slot_start;
    if (localtime_r(&slot_start_t, &tm_buf) != NULL) {
        add_info("RX %s %02i:%02i:%02i", cfg_digital_label_get(),
                 tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    }
}

static void on_tick_cb(const slot_info_t *info, bool new_slot,
                       float sec_since_slot_start, void *ctx) {
    (void)ctx;
    bool have_tx_msg = tx_msg.msg[0] != '\0';

    float tx_max_delay = (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8)
                         ? MAX_TX_START_DELAY : MAX_TX_START_DELAY_FT4;
    if ((sec_since_slot_start < tx_max_delay) && have_tx_msg) {
        if ((tx_time_slot == info->odd) && subject_get_int(tx_enabled)) {
            if (s_auto_dnf) {
                auto_dnf_clear_for_tx(s_auto_dnf);
            }
            state = TX_PROCESS;

            bool sent_grid = is_grid_exchange_msg(tx_msg.msg);
            ft8_log_tx((time_t)info->slot_start,
                       (uint64_t)subject_get_int(cfg_cur.fg_freq),
                       params.ft8_tx_freq.x, tx_msg.msg);
            add_tx_text(tx_msg.msg);
            tx_worker(sec_since_slot_start);

            if (sent_grid && autosel_qso_active && (autosel_grid_tx_count < 255)) {
                autosel_grid_tx_count++;
            }
            if (tx_msg.repeats > 0) {
                tx_msg.repeats--;
            }
            if (tx_msg.repeats == 0) {
                bool was_cq = (strncmp(tx_msg.msg, "CQ", 2) == 0);
                if (!was_cq && ftx_qso_processor_has_current(qso_processor)) {
                    qso_tx_exhausted = true;
                    if (is_grid_exchange_msg(tx_msg.msg)) {
                        autosel_blacklist_add(qso_active_call);
                    }
                    if ((auto_sel_mode != AUTO_SEL_OFF) && !cq_paused_for_qso) {
                        autosel_recover_mode = true;
                    }
                }
                if (was_cq) {
                    subject_set_int(cq_enabled, false);
                }
                tx_msg.msg[0] = '\0';
                tx_msg.force_free_text = false;
                if (cq_paused_for_qso && !ftx_qso_processor_has_current(qso_processor)) {
                    subject_set_int(cq_enabled, true);
                    cq_paused_for_qso = false;
                    schedule_cq_tx_if_needed();
                }
                if (!was_cq && !cq_paused_for_qso && !ftx_qso_processor_has_current(qso_processor) &&
                    cq_enabled && subject_get_int(cq_enabled) && (tx_msg.msg[0] == '\0')) {
                    schedule_cq_tx_if_needed();
                }
            }
        }
    }
}

/* clip_i32 / overlay_update / restore / scheduler trampolines all live in
 * ft8/auto_dnf.c. Anything still poking at them goes through the module
 * API: auto_dnf_on_psd / auto_dnf_clear_for_tx / auto_dnf_restore_entry. */

/* truncate_table / add_msg_cb / draw_part_*_cb live in ft8/table_view.c.
 * Worker thread feeds rows via scheduler_put(table_view_add_msg_cb, ...). */

static void deferred_destruct_cb(void *arg) {
    (void)arg;
    dialog_destruct();
}

static void key_cb(lv_event_t * e) {
    uint32_t *key_ptr = (uint32_t *) lv_event_get_param(e);
    uint32_t key = *key_ptr;

    /* In FT8 list only: swap LEFT/RIGHT so remote + (top) = scroll up, - (bottom) = scroll down */
    if (key == LV_KEY_LEFT) {
        *key_ptr = LV_KEY_RIGHT;
        key = LV_KEY_RIGHT;
    } else if (key == LV_KEY_RIGHT) {
        *key_ptr = LV_KEY_LEFT;
        key = LV_KEY_LEFT;
    }

    switch (key) {
        case LV_KEY_ESC:
            /* Defer so we do not delete the dialog from within its own key handler
             * (avoids crash and ensures destruct_cb / DNF restore run after event). */
            lv_async_call(deferred_destruct_cb, NULL);
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}

static void destruct_cb() {

    /* Stop active work immediately to avoid race conditions with observers/table. */
    worker_done();

    /* Restore DNF first so it is applied even if we crash during teardown
     * (e.g. when closing via VOL/APP and object is torn down from event handler).
     * auto_dnf_destroy() also restores entry as a safety net. */
    if (s_auto_dnf) {
        auto_dnf_restore_entry(s_auto_dnf);
    }


    /* table_view_destroy below cleans up the LVGL table; pool is static
     * and needs no per-cell free. */
    keyboard_close();

    /* Free any pending AutoSel candidates and clear blacklist state. */
    autosel_clear_unseen();
    autosel_blacklist_clear_all();

    mem_load(MEM_BACKUP_ID);

    main_screen_lock_mode(false);
    main_screen_lock_ab(false);
    main_screen_lock_freq(false);
    main_screen_lock_band(false);

    radio_set_pwr(subject_get_float(cfg.pwr.val));
    adif_log_close(ft8_log);
    ft8_log_close();

    /* Tear down the LVGL table object. The cell pool is static so there
     * is nothing else to free. */
    table_view_destroy();

    dsp_set_waterfall_enabled(true);
    dsp_set_spectrum_enabled(true);

    /* Auto-DNF context owns observers + LVGL overlay objects (already torn
     * down with the dialog). Destroy releases the max-pool buffer. */
    if (s_auto_dnf) {
        auto_dnf_destroy(s_auto_dnf);
        s_auto_dnf = NULL;
    }
}

static void load_band(int8_t dir) {
    cfg_digital_type_t type;
    switch (subject_get_int(cfg.ft8_protocol.val)) {
        case FTX_PROTOCOL_FT8:
            type = CFG_DIG_TYPE_FT8;
            lv_finder_set_width(finder, FT8_WIDTH_HZ);
            break;

        case FTX_PROTOCOL_FT4:
            type = CFG_DIG_TYPE_FT4;
            lv_finder_set_width(finder, FT4_WIDTH_HZ);
            break;
    }
    bool res = cfg_digital_load(dir, type);
    if (res) {
        msg_update_text_fmt("%s", cfg_digital_label_get());
    }
}

/// @brief Clean waterfall and table
static void clean_screen() {
    table_view_reset();
    lv_waterfall_clear_data(waterfall);

    int32_t c = LV_KEY_UP;
    lv_event_send(table, LV_EVENT_KEY, &c);
}

static void band_cb(lv_event_t * e) {
    int8_t dir;

    if (lv_event_get_code(e) == EVENT_BAND_UP) {
        dir = 1;
    } else {
        dir = -1;
    }

    load_band(dir);

    worker_done();
    worker_init();
    clean_screen();
}

static void msg_timer(lv_timer_t *t) {
    lv_anim_set_values(&fade, lv_obj_get_style_opa_layered(table, 0), LV_OPA_COVER);
    lv_anim_start(&fade);
    timer = NULL;
}

static void fade_anim(void * obj, int32_t v) {
    lv_obj_set_style_opa_layered(obj, v, 0);
}

static void fade_ready(lv_anim_t * a) {
    fade_run = false;
}

static void set_freq(uint32_t freq);

static void set_freq_ui_cb(void *param) {
    uint32_t freq;
    memcpy(&freq, param, sizeof(freq));
    lv_finder_set_value(finder, freq);
    lv_obj_invalidate(finder);
}

static void set_freq_ui_async(uint32_t freq) {
    scheduler_put(set_freq_ui_cb, &freq, sizeof(freq));
}

static void finder_set_cursor_cb(void *param) {
    uint32_t freq_hz;
    memcpy(&freq_hz, param, sizeof(freq_hz));
    lv_finder_set_cursor(finder, freq_hz);
}

static void finder_set_cursor_async(uint32_t freq_hz) {
    scheduler_put(finder_set_cursor_cb, &freq_hz, sizeof(freq_hz));
}

static void set_freq(uint32_t freq) {
    if (freq > filter_high) {
        freq = filter_high;
    }

    if (freq < filter_low) {
        freq = filter_low;
    }

    params_uint16_set(&params.ft8_tx_freq, freq);

    /* UI updates must run on the LVGL/UI thread; the param update above is
     * time-critical (it affects actual TX frequency selection).
     */
    set_freq_ui_async(freq);
}

static void rotary_cb(int32_t diff) {
    uint32_t abs_diff = abs(diff);
    if (abs_diff > 3) {
        diff *= (abs_diff < 6) ? 5 : 10;
    }
    uint32_t f = params.ft8_tx_freq.x + diff;
    f = limit(f, filter_low, filter_high - (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? FT8_WIDTH_HZ : FT4_WIDTH_HZ));

    set_freq(f);

    if (!fade_run) {
        fade_run = true;
        lv_anim_set_values(&fade, lv_obj_get_style_opa_layered(table, 0), LV_OPA_TRANSP);
        lv_anim_start(&fade);
    }

    if (timer) {
        lv_timer_reset(timer);
    } else {
        timer = lv_timer_create(msg_timer, 1000, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }

}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    dsp_set_waterfall_enabled(false);
    dsp_set_spectrum_enabled(false);

    /* Load tuning parameters before creating the auto_dnf module so it gets
     * the user's current values. */
    ft8_params_load(&ft8_tuning);

    /* Auto DNF: create module instance (registers band/att/pre observers,
     * snapshots the current DNF state for restore-on-exit). */
    s_auto_dnf = auto_dnf_create(&ft8_tuning);
    auto_dnf_snapshot_entry(s_auto_dnf);

    /* Ensure no stale AutoSel state leaks across dialog reopen. */
    autosel_clear_unseen();
    autosel_blacklist_clear_all();
    autosel_user_blacklist_load();
    autosel_recover_mode = false;
    qso_tx_exhausted = false;
    pending_to_me_valid = false;
    qso_active_call[0] = '\0';
    cq_paused_for_qso = false;
    tx_msg.msg[0] = '\0';
    tx_msg.repeats = 0;
    tx_msg.force_free_text = false;
    autosel_qso_active = false;
    autosel_grid_tx_count = 0;
    pending_grid_to_me_valid = false;

    lv_obj_add_event_cb(dialog.obj, band_cb, EVENT_BAND_UP, NULL);
    lv_obj_add_event_cb(dialog.obj, band_cb, EVENT_BAND_DOWN, NULL);

    if (!cq_enabled) {
        cq_enabled = subject_create_int(false);
        button_tx_cq_en_dis.subj = &cq_enabled;
    } else {
        subject_set_int(cq_enabled, false);
    }
    if (!tx_enabled) {
        tx_enabled = subject_create_int(true);
        button_tx_call_en_dis.subj = &tx_enabled;
    }

    buttons_load_page(&btn_page_1);

    /* Waterfall */

    waterfall = lv_waterfall_create(dialog.obj);

    lv_obj_add_style(waterfall, &waterfall_style, 0);
    lv_obj_clear_flag(waterfall, LV_OBJ_FLAG_SCROLLABLE);

    lv_waterfall_set_palette(waterfall, (lv_color_t*)wf_palette, 256);
    lv_waterfall_set_size(waterfall, WIDTH, 325);
    lv_waterfall_set_min(waterfall, -60);

    lv_obj_set_pos(waterfall, 13, 13);

    /* Auto DNF overlay (blue peak lines + delta_db label, drawn over the
     * waterfall). filter_low/high are populated below; we patch them in once
     * load_band() has run. For now use the cached values. */
    auto_dnf_build_overlay(s_auto_dnf, dialog.obj,
                           13, 13, 325, WIDTH,
                           subject_get_int(cfg_cur.filter.low),
                           subject_get_int(cfg_cur.filter.high));

    /* Freq finder */

    finder = lv_finder_create(waterfall);

    lv_finder_set_width(finder, 50);
    lv_finder_set_value(finder, params.ft8_tx_freq.x);

    lv_obj_set_size(finder, WIDTH, 325);
    lv_obj_set_pos(finder, 0, 0);

    lv_obj_set_style_radius(finder, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(finder, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(finder, LV_OPA_0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(finder, bg_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(finder, LV_OPA_50, LV_PART_INDICATOR);

    lv_obj_set_style_border_width(finder, 1, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(finder, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(finder, LV_OPA_50, LV_PART_INDICATOR);

    /* Table view (cell ring pool, draw cbs, ESC/VOL key handling). */
    table_view_build(dialog.obj, 13, 13 + 55, WIDTH, 325 - 55);
    table_view_set_press_cb(on_table_press);
    table_view_actions_t tv_actions = {
        .on_close      = on_table_close,
        .on_vol_change = on_table_vol_change,
    };
    table_view_set_actions(&tv_actions);

    /* Fade */

    lv_anim_init(&fade);
    lv_anim_set_var(&fade, table);
    lv_anim_set_time(&fade, 250);
    lv_anim_set_exec_cb(&fade, fade_anim);
    lv_anim_set_ready_cb(&fade, fade_ready);

    /* * */

    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);

    mem_save(MEM_BACKUP_ID);
    load_band(0);

    filter_low = subject_get_int(cfg_cur.filter.low);
    filter_high = subject_get_int(cfg_cur.filter.high);

    lv_finder_set_range(finder, filter_low, filter_high);

    if (params.qth.x[0] != '\0' && qth_grid_check(params.qth.x)) {
        qth_str_to_pos(params.qth.x, &cur_lat, &cur_lon);
        local_qth_valid = true;
    } else {
        cur_lat = 0.0;
        cur_lon = 0.0;
        local_qth_valid = false;
    }

    main_screen_lock_ab(true);
    main_screen_lock_mode(true);
    main_screen_lock_freq(true);
    main_screen_lock_band(true);

    worker_init();

    /* Logger */
    ft8_log = adif_log_init("/mnt/ft_log.adi");

    if (subject_get_float(cfg.pwr.val) > MAX_PWR) {
        radio_set_pwr(MAX_PWR);
        msg_schedule_text_fmt("Power was limited to %0.0fW", MAX_PWR);
    }

    // setup gain offset
    float target_pwr = LV_MIN(subject_get_float(cfg.pwr.val), MAX_PWR);
    // patched firmware has a true power control
    base_gain_offset = -16.4f + log10f(target_pwr) * 10.0f;
}

/* Buttons */

const char *cq_all_label_getter() {
    static char buf[32];
    sprintf(buf, "Show:\n%s", subject_get_int(cfg.ft8_show_all.val) ? "All": "CQ");
    return buf;
}

const char *protocol_label_getter() {
    static char buf[32];
    sprintf(buf, "Mode:\n%s", subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? "FT8": "FT4");
    return buf;
}

const char *tx_cq_label_getter() {
    static char buf[32];
    sprintf(buf, "TX CQ:\n%s", subject_get_int(cq_enabled) ? "Enabled": "Disabled");
    return buf;
}

const char *tx_call_label_getter() {
    static char buf[32];
    sprintf(buf, "TX Call:\n%s", subject_get_int(tx_enabled) ? "Enabled": "Disabled");
    return buf;
}

const char *hold_freq_label_getter() {
    static char buf[32];
    sprintf(buf, "Hold Freq:\n%s", subject_get_int(cfg.ft8_hold_freq.val) ? "Enabled": "Disabled");
    return buf;
}

const char *auto_label_getter() {
    static char buf[32];
    sprintf(buf, "Auto:\n%s", subject_get_int(cfg.ft8_auto.val) ? "Enabled": "Disabled");
    return buf;
}

const char *auto_dnf_label_getter() {
    static char buf[32];
    sprintf(buf, "Auto DNF:\n%s", subject_get_int(cfg.ft8_auto_dnf.val) ? "On" : "Off");
    return buf;
}

const char *xit_label_getter() {
    static char buf[32];
    sprintf(buf, "XIT:\n%s", subject_get_int(cfg.ft8_xit.val) ? "On" : "Off");
    return buf;
}

const char *auto_sel_label_getter() {
    static char buf[32];
    const char *s;
    switch (auto_sel_mode) {
        case AUTO_SEL_OFF: s = "Off"; break;
        case AUTO_SEL_FIRST: s = "First"; break;
        case AUTO_SEL_FARTHEST: s = "Farthest"; break;
        case AUTO_SEL_HIGHEST_SNR: s = "Best SNR"; break;
        case AUTO_SEL_NEW_GRID: s = "New Grid"; break;
        default: s = "?"; break;
    }
    sprintf(buf, "AutoSel:\n%s", s);
    return buf;
}

void mode_auto_sel_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    auto_sel_mode = (auto_sel_mode + 1) % 5;
    const char *s;
    switch (auto_sel_mode) {
        case AUTO_SEL_OFF: s = "Off"; break;
        case AUTO_SEL_FIRST: s = "First"; break;
        case AUTO_SEL_FARTHEST: s = "Farthest"; break;
        case AUTO_SEL_HIGHEST_SNR: s = "Best SNR"; break;
        case AUTO_SEL_NEW_GRID: s = "New Grid"; break;
        default: s = "?"; break;
    }
    msg_schedule_text_fmt("Auto select: %s", s);
    /* If user turned AutoSel Off, clear any accumulated unseen candidates. */
    if (auto_sel_mode == AUTO_SEL_OFF) {
        autosel_clear_unseen();
        autosel_blacklist_clear_all();
        autosel_qso_active = false;
        autosel_grid_tx_count = 0;
        pending_grid_to_me_valid = false;
        autosel_recover_mode = false;
    }

    /* auto_sel_mode is not a Subject-backed value, so force a button label refresh. */
    buttons_refresh(btn);
}

static void show_cq_all_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    subject_set_int(cfg.ft8_show_all.val, !subject_get_int(cfg.ft8_show_all.val));
}

static void mode_ft4_ft8_cb(struct button_item_t *btn) {
    if (disable_buttons) return;

    ftx_protocol_t proto = subject_get_int(cfg.ft8_protocol.val);
    if (proto == FTX_PROTOCOL_FT8)
        proto = FTX_PROTOCOL_FT4;
    else {
        proto = FTX_PROTOCOL_FT8;
    }
    subject_set_int(cfg.ft8_protocol.val, proto);
    subject_set_int(cq_enabled, false);

    worker_done();
    worker_init();
    clean_screen();
    load_band(0);
}

static void mode_auto_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    bool new_val = !subject_get_int(cfg.ft8_auto.val);
    subject_set_int(cfg.ft8_auto.val, new_val);
    ftx_qso_processor_set_auto(qso_processor, new_val);
}

static void auto_dnf_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    bool new_val = !subject_get_int(cfg.ft8_auto_dnf.val);
    subject_set_int(cfg.ft8_auto_dnf.val, new_val);
    if (!new_val && s_auto_dnf) {
        auto_dnf_restore_entry(s_auto_dnf);
    }
}

static void xit_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    subject_set_int(cfg.ft8_xit.val, !subject_get_int(cfg.ft8_xit.val));
}

static void hold_tx_freq_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    subject_set_int(cfg.ft8_hold_freq.val, !subject_get_int(cfg.ft8_hold_freq.val));
}

static void tx_cq_en_dis_cb(struct button_item_t *btn) {
    if (disable_buttons) return;

    if (!subject_get_int(cq_enabled)){
        if (strlen(params.callsign.x) == 0) {
            msg_schedule_text_fmt("Call sign required");
            return;
        }
        subject_set_int(cq_enabled, true);
        subject_set_int(tx_enabled, true);

        make_cq_msg(params.callsign.x, params.qth.x, params.ft8_cq_modifier.x, tx_msg.msg);
        tx_msg.force_free_text = false;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        float time_since_slot_start;
        tx_time_slot = !get_time_slot(now, &time_since_slot_start);
        float max_delay = (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8) ? MAX_TX_START_DELAY : MAX_TX_START_DELAY_FT4;
        if (time_since_slot_start < max_delay) {
            tx_time_slot = !tx_time_slot;
        }

        if (tx_msg.msg[2] == '_') {
            msg_schedule_text_fmt("Next TX: CQ %s", tx_msg.msg + 3);
        } else {
            msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        }
        tx_msg.repeats = subject_get_int(cfg.ft8_max_repeats.val);
        ftx_qso_processor_reset(qso_processor);
        lv_finder_clear_cursor(finder);
        autosel_recover_mode = false;
        qso_tx_exhausted = false;
        pending_to_me_valid = false;
        qso_active_call[0] = '\0';
        autosel_qso_active = false;
        autosel_grid_tx_count = 0;
        pending_grid_to_me_valid = false;
    } else {
        if (state == TX_PROCESS) {
            state = RX_PROCESS;
        }
        subject_set_int(cq_enabled, false);
        /* If user manually disables CQ, clear the paused-for-qso flag
         * so automatic restore does not re-enable CQ against user intent. */
        cq_paused_for_qso = false;
        tx_msg.msg[0] = '\0';
        tx_msg.force_free_text = false;
        autosel_recover_mode = false;
        qso_tx_exhausted = false;
        pending_to_me_valid = false;
        qso_active_call[0] = '\0';
        autosel_qso_active = false;
        autosel_grid_tx_count = 0;
        pending_grid_to_me_valid = false;
    }
}

static void tx_call_en_dis_cb(struct button_item_t *btn) {
    if (disable_buttons)
        return;

    if (!subject_get_int(tx_enabled)) {
        if (strlen(params.callsign.x) == 0) {
            msg_schedule_text_fmt("Call sign required");
            return;
        }
        subject_set_int(tx_enabled, true);
    } else {
        if (state == TX_PROCESS) {
            state = RX_PROCESS;
        }
        subject_set_int(tx_enabled, false);
    }
}

static void tx_call_off() {
    state = RX_PROCESS;
    /* If a TX is in progress, signal the play loop to break out at the next
     * 2k-sample boundary instead of hanging until the slot ends. */
    tx_worker_request_abort();
    subject_set_int(tx_enabled, false);
}

static void cq_modifier_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    keyboard_open();
}

static void time_sync(struct button_item_t *btn) {
    time_t now = time(NULL);
    uint8_t sec = now % 60;
    float drift, slot_time;
    switch (subject_get_int(cfg.ft8_protocol.val)) {
        case FTX_PROTOCOL_FT4:
            slot_time = FT4_SLOT_TIME;
            break;

        case FTX_PROTOCOL_FT8:
            slot_time = FT8_SLOT_TIME;
            break;
    }
    drift = fmodf(sec + slot_time / 2, slot_time) - slot_time / 2;
    struct timespec tp;

    now -= (int) drift;
    tp.tv_sec = now;
    tp.tv_nsec = 0;

    int res = clock_settime(CLOCK_REALTIME, &tp);
    if (res != 0)
    {
        LV_LOG_ERROR("Can't set system time: %s\n", strerror(errno));
        return;
    }
}

static void force_save_qso(struct button_item_t *btn) {
    if (ftx_qso_processor_can_save_qso(qso_processor)) {
        ftx_qso_processor_force_save_qso(qso_processor);
    } else {
        msg_schedule_text_fmt("Can't save incomplete QSO");
    }
}

/* table_view press handler: replicates the legacy cell_press_cb business
 * logic. Invoked from table_view's cell-pressed event after it has
 * resolved the row's pool handle to a cell_data_t. */
static void on_table_press(const cell_data_t *cell_data) {
    if (state == TX_PROCESS) {
        tx_call_off();
        return;
    }

    if ((cell_data == NULL) ||
        (cell_data->cell_type == CELL_TX_MSG) ||
        (cell_data->cell_type == CELL_RX_INFO)
    ) {
        msg_schedule_text_fmt("What should I do about it?");
        return;
    }

    char prev_msg[sizeof(tx_msg.msg)];
    memcpy(prev_msg, tx_msg.msg, sizeof(prev_msg));
    ftx_qso_processor_start_qso(qso_processor, (ftx_msg_meta_t *)&cell_data->meta, &tx_msg);
    if (strcmp(prev_msg, tx_msg.msg) != 0) {
        tx_msg.force_free_text = false;
    }
    /* Log QSO entry for debugging even if TX is later aborted/invalid. */
    ft8_log_qso(ft8_get_current_slot_start(), cell_data->meta.call_de);
    if (strlen(tx_msg.msg) > 0) {
        lv_finder_set_cursor(finder, cell_data->meta.freq_hz);
        if (!subject_get_int(cfg.ft8_hold_freq.val)) {
            set_freq(cell_data->meta.freq_hz);
        }
        tx_time_slot = !cell_data->odd;
        subject_set_int(tx_enabled, true);
        add_info("Start QSO with %s", cell_data->meta.call_de);
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        autosel_recover_mode    = false;
        qso_tx_exhausted        = false;
        pending_to_me_valid     = false;
        pending_grid_to_me_valid= false;
        autosel_qso_active      = false;
        autosel_grid_tx_count   = 0;
        strncpy(qso_active_call, cell_data->meta.call_de, sizeof(qso_active_call) - 1);
        qso_active_call[sizeof(qso_active_call) - 1] = '\0';
    } else {
        msg_schedule_text_fmt("Invalid message");
        tx_call_off();
    }
}

/* table_view actions: ESC -> async dialog destruct, VOL keys -> radio vol. */
static void on_table_close(void) {
    lv_async_call(deferred_destruct_cb, NULL);
}
static void on_table_vol_change(int32_t delta) {
    radio_change_vol(delta);
}

static void keyboard_open() {
    lv_group_remove_obj(table);
    textarea_window_open(keyboard_ok_cb, keyboard_cancel_cb);
    lv_obj_t *text = textarea_window_text();

    lv_textarea_set_accepted_chars(text,
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    );

    if (strlen(params.ft8_cq_modifier.x) > 0) {
        textarea_window_set(params.ft8_cq_modifier.x);
    } else {
        lv_obj_t *text = textarea_window_text();
        lv_textarea_set_placeholder_text(text, " CQ modifier");
    }
    disable_buttons = true;
}

static void keyboard_close() {
    textarea_window_close();
    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);
    disable_buttons = false;
}

static bool keyboard_cancel_cb() {
    keyboard_close();
    return true;
}

static bool keyboard_ok_cb() {
    char *cq_mod = (char *)textarea_window_get();
    if ((strlen(cq_mod) > 0) && !is_cq_modifier(cq_mod)) {
        msg_schedule_text_fmt("Unsupported CQ modifier");
        return false;
    }
    params_str_set(&params.ft8_cq_modifier, cq_mod);
    keyboard_close();
    return true;
}

#define FT8_FREETEXT_FILE "/mnt/ft8_freetext.txt"
#define FT8_FREETEXT_MAX_LEN 13
#define FT8_FREETEXT_ACCEPTED_CHARS " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?"

static void ft8_freetext_sanitize(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        char c = in[i];
        if (c == '\n' || c == '\r') {
            break;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (strchr(FT8_FREETEXT_ACCEPTED_CHARS, c) == NULL) {
            continue;
        }
        if (j == 0 && c == ' ') {
            continue;
        }
        if (j + 1 >= out_size) {
            break;
        }
        if (j >= FT8_FREETEXT_MAX_LEN) {
            break;
        }
        out[j++] = c;
    }
    while (j > 0 && out[j - 1] == ' ') {
        j--;
    }
    out[j] = '\0';
}

static void ft8_freetext_load(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    FILE *fp = fopen(FT8_FREETEXT_FILE, "r");
    if (!fp) return;

    char buf[128];
    buf[0] = '\0';
    (void)fgets(buf, sizeof(buf), fp);
    fclose(fp);

    ft8_freetext_sanitize(buf, out, out_size);
}

static bool ft8_freetext_save(const char *text) {
    FILE *fp = fopen(FT8_FREETEXT_FILE, "w");
    if (!fp) return false;
    if (text && text[0] != '\0') {
        fputs(text, fp);
    }
    fputc('\n', fp);
    fclose(fp);
    return true;
}

static void free_msg_cb(struct button_item_t *btn) {
    (void)btn;
    free_msg_open();
}

static void free_msg_open() {
    if (!table) {
        return;
    }

    lv_group_remove_obj(table);
    textarea_window_open_w_label(free_msg_ok_cb, free_msg_cancel_cb, "Free MSG");
    lv_obj_t *text = textarea_window_text();

    lv_textarea_set_one_line(text, true);
    lv_textarea_set_max_length(text, FT8_FREETEXT_MAX_LEN);
    lv_textarea_set_accepted_chars(text, FT8_FREETEXT_ACCEPTED_CHARS);

    char def[FT8_FREETEXT_MAX_LEN + 1];
    ft8_freetext_load(def, sizeof(def));
    if (def[0] != '\0') {
        textarea_window_set(def);
    } else {
        lv_textarea_set_placeholder_text(text, " FREE TEXT");
    }
    disable_buttons = true;
}

static void free_msg_close() {
    textarea_window_close();
    if (table) {
        lv_group_add_obj(keyboard_group, table);
        lv_group_set_editing(keyboard_group, true);
    }
    disable_buttons = false;
}

static bool free_msg_cancel_cb() {
    free_msg_close();
    return true;
}

static bool free_msg_ok_cb() {
    const char *raw = textarea_window_get();
    char clean[FT8_FREETEXT_MAX_LEN + 1];
    ft8_freetext_sanitize(raw, clean, sizeof(clean));
    if (clean[0] == '\0') {
        msg_schedule_text_fmt("Empty Free MSG");
        return false;
    }

    if (!ft8_freetext_save(clean)) {
        msg_schedule_text_fmt("Save Free MSG failed");
    }

    if (!qso_processor) {
        msg_schedule_text_fmt("FT8 not ready");
        return false;
    }
    if (!tx_enabled || !cq_enabled) {
        msg_schedule_text_fmt("FT8 not ready");
        return false;
    }

    if (ftx_qso_processor_has_current(qso_processor)) {
        msg_schedule_text_fmt("QSO active, cannot Free MSG");
        return false;
    }
    if (tx_msg.msg[0] != '\0') {
        msg_schedule_text_fmt("TX busy, cannot Free MSG");
        return false;
    }

    /* Validate the free-text message fits in a 77-bit FT8 frame. */
    {
        ftx_message_t tmp_msg;
        ftx_message_rc_t rc = ftx_message_encode(&tmp_msg, NULL, clean);
        if (rc != FTX_MESSAGE_RC_OK) {
            msg_schedule_text_fmt("Free MSG too long for FT8");
            return false;
        }
    }

    strncpy(tx_msg.msg, clean, sizeof(tx_msg.msg) - 1);
    tx_msg.msg[sizeof(tx_msg.msg) - 1] = '\0';
    tx_msg.repeats = 1;
    tx_msg.force_free_text = true;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    float time_since_slot_start = 0.0f;
    tx_time_slot = !get_time_slot(now, &time_since_slot_start);
    float max_delay = (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8) ? MAX_TX_START_DELAY : MAX_TX_START_DELAY_FT4;
    if (time_since_slot_start < max_delay) {
        tx_time_slot = !tx_time_slot;
    }

    subject_set_int(tx_enabled, true);
    msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);

    free_msg_close();
    return true;
}

static void audio_cb(unsigned int n, float complex *samples) {
    if (s_audio_worker) {
        audio_worker_feed(s_audio_worker, n, samples);
    }
}

static bool get_time_slot(struct timespec now, float *sec_since_start) {
    bool cur_odd;
    float sec = (now.tv_sec % 60) + now.tv_nsec / 1.0e9f;

    switch (subject_get_int(cfg.ft8_protocol.val)) {
    case FTX_PROTOCOL_FT4:
        cur_odd = (int)(sec / FT4_SLOT_TIME) % 2;
        *sec_since_start = fmodf(sec, FT4_SLOT_TIME);
        break;

        case FTX_PROTOCOL_FT8:
        cur_odd = (int)(sec / FT8_SLOT_TIME) % 2;
        *sec_since_start = fmodf(sec, FT8_SLOT_TIME);
        break;
    }
    return cur_odd;
}


/* CQ message composer + scheduler module. */
#include "ft8/cq_scheduler.h"

/**
 * Create CQ TX message - thin wrapper around cq_make_message().
 */
static void make_cq_msg(const char *callsign, const char *qth, const char *cq_mod, char *text) {
    cq_make_message(callsign, qth, cq_mod,
                    text, FTX_MAX_MESSAGE_LENGTH,
                    subject_get_int(cfg.ft8_omit_cq_qth.val));
}

/* Bridge to ft8/tx_worker.c - the actual TX play loop and ALC-driven gain
 * correction live there. tx_call_off() requests an abort via the module's
 * atomic flag (replaces the old `state != TX_PROCESS` early-out). */
#include "ft8/tx_worker.h"

static void tx_worker(float sec_since_slot_start) {
    tx_worker_clear_abort();
    tx_worker_run(tx_msg.msg, tx_msg.force_free_text,
                  AUDIO_PLAY_RATE, base_gain_offset,
                  sec_since_slot_start);
    state = RX_PROCESS;
}

/**
 * Add INFO message to the table
 */
static void add_info(const char * fmt, ...) {
    va_list     args;
    cell_data_t  cell_data = {0};
    cell_data.cell_type = CELL_RX_INFO;

    va_start(args, fmt);
    vsnprintf(cell_data.text, sizeof(cell_data.text), fmt, args);
    va_end(args);
    cell_data.text[sizeof(cell_data.text) - 1] = '\0';

    scheduler_put(table_view_add_msg_cb, &cell_data, sizeof(cell_data_t));
}
static void add_tx_text(const char * text) {
    cell_data_t  cell_data = {0};
    cell_data.cell_type = CELL_TX_MSG;

    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    cell_data.text[sizeof(cell_data.text) - 1] = '\0';
    if (strncmp(cell_data.text, "CQ_", 3) == 0) {
        cell_data.text[2] = ' ';
    }
    scheduler_put(table_view_add_msg_cb, &cell_data, sizeof(cell_data_t));
}

static void add_rx_text(int16_t snr, const char * text, slot_info_t *s_info, float freq_hz, float time_sec) {
    bool had_qso = ftx_qso_processor_has_current(qso_processor);
    ftx_msg_meta_t meta = {0};
    meta.freq_hz = freq_hz;
    meta.time_sec = time_sec;
    char * old_msg = strdup(tx_msg.msg);
    bool old_msg_alloced = true;
    if (!old_msg) {
        old_msg = "";
        old_msg_alloced = false;
    }
    ftx_qso_processor_add_rx_text(qso_processor, text, snr, &meta, &tx_msg);

    if (!had_qso && ftx_qso_processor_has_current(qso_processor) && s_info) {
        ft8_log_qso(s_info->slot_start, meta.call_de);
    }

    if ((strlen(tx_msg.msg) > 0) && (strcmp(old_msg, tx_msg.msg) != 0)) {
        tx_msg.force_free_text = false;
        finder_set_cursor_async(meta.freq_hz);
        if (!subject_get_int(cfg.ft8_hold_freq.val)) {
            set_freq(freq_hz);
        }
        tx_time_slot = !s_info->odd;
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        if (meta.to_me && (meta.call_de[0] != '\0')) {
            strncpy(qso_active_call, meta.call_de, sizeof(qso_active_call) - 1);
            qso_active_call[sizeof(qso_active_call) - 1] = '\0';
        }
        if (subject_get_int(cq_enabled)) {
            /* Pause CQ while handling this QSO so we don't keep CQing others.
             * Remember that CQ was enabled so we can restore it after QSO.
             */
            cq_paused_for_qso = true;
            subject_set_int(cq_enabled, false);
        }

        /* Any new outgoing message means we're no longer in the "TX exhausted" state. */
        qso_tx_exhausted = false;
        autosel_recover_mode = false;

        /* If TX message changed (progress), reset AutoSel grid tracking. */
        autosel_grid_tx_count = 0;
        pending_grid_to_me_valid = false;
    }
    if (old_msg_alloced) free(old_msg);

    /* Priority (2): after TX is exhausted, if another station calls to me,
     * remember it so rx_worker() can switch QSO before AutoSel.
     */
    if (qso_tx_exhausted && meta.to_me && (meta.call_de[0] != '\0')) {
        if ((qso_active_call[0] == '\0') || (strcmp(qso_active_call, meta.call_de) != 0)) {
            if (!pending_to_me_valid) {
                pending_to_me_meta = meta;
                pending_to_me_odd = s_info->odd;
                pending_to_me_valid = true;
            }
        }
    }

    /* Priority (grid-stage): while AutoSel is repeatedly sending our grid to a CQ caller,
     * remember the first other station that sends a GRID message to us.
     * This is acted upon at the start of the 3rd grid TX.
     */
    if (autosel_qso_active && is_grid_exchange_msg(tx_msg.msg) && meta.to_me && (meta.type == FTX_MSG_TYPE_GRID) && (meta.call_de[0] != '\0')) {
        if ((qso_active_call[0] == '\0') || (strcmp(qso_active_call, meta.call_de) != 0)) {
            if (!pending_grid_to_me_valid) {
                pending_grid_to_me_meta = meta;
                pending_grid_to_me_odd = s_info->odd;
                pending_grid_to_me_valid = true;
            }
        }
    }

    ft8_cell_type_t cell_type;
    if (meta.to_me) {
        cell_type = CELL_RX_TO_ME;
    } else if (meta.type == FTX_MSG_TYPE_CQ) {
        cell_type = CELL_RX_CQ;
    } else if (!subject_get_int(cfg.ft8_show_all.val)) {
        return;
    } else {
        cell_type = CELL_RX_MSG;
    }

    cell_data_t  cell_data = {0};
    if (meta.type == FTX_MSG_TYPE_CQ) {
        cell_data.worked_type = qso_log_search_worked(
            meta.call_de,
            subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
            qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq))
        );
    }

    /* Collect unseen CQ callers for auto-selection if we're not in a QSO.
     * Compute distance first and only collect when AutoSel is enabled.
     * Use local_qth_valid to ensure we don't use (0,0) as a false valid position. */
    int computed_dist = 0;
    if (local_qth_valid && strlen(meta.grid) > 0 && qth_grid_check(meta.grid)) {
        double lat, lon;
        qth_str_to_pos(meta.grid, &lat, &lon);
        computed_dist = (int) qth_pos_dist(lat, lon, cur_lat, cur_lon);
    }
    if ((meta.type == FTX_MSG_TYPE_CQ) && !meta.to_me) {
        /* Track CQ activity for blacklisted stations so they only clear after
         * X consecutive cycles of not being heard CQ'ing.
         */
        autosel_blacklist_mark_seen(meta.call_de);
        if ((auto_sel_mode != AUTO_SEL_OFF) &&
            (!ftx_qso_processor_has_current(qso_processor) || autosel_recover_mode)
        ) {
            autosel_add_candidate(&meta, computed_dist, meta.local_snr);
        }
    }

    cell_data.cell_type = cell_type;
    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    cell_data.text[sizeof(cell_data.text) - 1] = '\0';
    cell_data.meta = meta;
    cell_data.odd = s_info->odd;
    /* assign computed distance for display */
    if (local_qth_valid) {
        cell_data.dist = computed_dist;
    } else {
        cell_data.dist = 0;
    }
    /* Prefix '*' before grid if this grid hasn't been worked yet (DB check) */
    if (meta.grid[0] != '\0') {
        bool grid_worked = qso_log_search_worked_grid(
            meta.grid,
            subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
            qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq))
        );
        if (!grid_worked) {
            char tmp[64];
            char *pos = strstr(cell_data.text, meta.grid);
            if (pos) {
                size_t prefix_len = pos - cell_data.text;
                if (prefix_len >= sizeof(tmp) - 2) prefix_len = sizeof(tmp) - 3;
                snprintf(tmp, sizeof(tmp), "%.*s*%s", (int)prefix_len, cell_data.text, pos);
            } else {
                snprintf(tmp, sizeof(tmp), "*%s", cell_data.text);
            }
            strncpy(cell_data.text, tmp, sizeof(cell_data.text) - 1);
            cell_data.text[sizeof(cell_data.text) - 1] = '\0';
        }
    }
    scheduler_put(table_view_add_msg_cb, (void*)&cell_data, sizeof(cell_data_t));
}

static void received_message_cb(const char *text, int snr, float freq_hz, float time_sec, void *user_data) {
    slot_info_t *s_info = (slot_info_t *)user_data;
    ft8_log_rx_collect(s_info, freq_hz, snr, text);
    add_rx_text(snr, text, s_info, freq_hz, time_sec);
}


