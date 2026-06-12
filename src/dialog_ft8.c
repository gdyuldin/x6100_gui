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
#include "dsp.h"
#include "keyboard.h"
#include "events.h"
#include "buttons.h"
#include "main_screen.h"
#include "qth/qth.h"
#include "msg.h"
#include "util.h"
#include "recorder.h"
#include "textarea_window.h"
#include "dsp.h"

#include "ft8/audio_worker.h"
#include "ft8/auto_sel.h"
#include "ft8/cq_scheduler.h"
#include "ft8/table_view.h"
#include "ft8/tx_worker.h"
#include "widgets/lv_waterfall.h"
#include "widgets/lv_finder.h"

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
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

#define SAMPLE_RATE     (AUDIO_CAPTURE_RATE / AUDIO_DECIM)

#define WIDTH           771

#define UNKNOWN_SNR     99

#define MAX_PWR         5.0f

#define FT8_WIDTH_HZ    50
#define FT4_WIDTH_HZ    83

#define MAX_TX_START_DELAY 1.5f

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

/* Last decoded message meta, set by add_rx_text() and available to
 * on_message_cb extension code. Worker thread only. */
static ftx_msg_meta_t     last_rx_meta;
typedef enum {
    RX_PROCESS,
    TX_PROCESS,
} ft8_state_t;

/* ft8_cell_type_t and cell_data_t live in ft8/table_view.h */

/* slot_info_t lives in ft8/audio_worker.h */

static ft8_state_t state = RX_PROCESS;
static Subject    *tx_enabled;
static Subject    *cq_enabled;
static bool        tx_time_slot;

static ftx_tx_msg_t tx_msg;

/* The lv_table widget is owned by ft8/table_view; expose its handle as
 * `table` so existing fade/group/anim call sites need no rename. */
#define table (table_view_obj())

static lv_timer_t *timer = NULL;
static lv_anim_t   fade;
static bool        fade_run        = false;
static bool        disable_buttons = false;

static lv_obj_t *finder;
static lv_obj_t *waterfall;

/* Audio capture, decimation, FFT, decoder thread and stop-flag plumbing
 * all live in ft8/audio_worker.c. The dialog just creates/destroys the
 * worker and exposes a handful of callbacks. */
static audio_worker_t *audio_worker = NULL;

static adif_log         ft8_log;
static FTxQsoProcessor *qso_processor;

static double cur_lat, cur_lon;

static int32_t filter_low, filter_high;

static float base_gain_offset;

/* PSD staging for coalesced waterfall updates — written by worker thread,
 * drained by scheduler callback on LVGL task. */
#define PSD_STAGING_MAX     1024
static float             psd_staging[PSD_STAGING_MAX];
static size_t            psd_staging_len    = 0;
static bool              psd_flush_pending  = false;
static pthread_mutex_t   psd_mutex          = PTHREAD_MUTEX_INITIALIZER;

/* Serialises qso_processor (and its Candidate objects) between the audio
 * worker thread (add_rx_text / slot_end) and the LVGL thread (table press,
 * buttons, worker re-init). */
static pthread_mutex_t   qso_mutex          = PTHREAD_MUTEX_INITIALIZER;

/* Protects the audio_worker pointer: the PulseAudio capture thread feeds
 * samples through audio_cb while the LVGL thread may tear the worker down
 * (band change, FT4/FT8 switch, dialog close). */
static pthread_mutex_t   audio_worker_mutex = PTHREAD_MUTEX_INITIALIZER;

static void construct_cb(lv_obj_t *parent);
static void key_cb(lv_event_t * e);
static void destruct_cb();
static void audio_cb(unsigned int n, float *samples);
static void rotary_cb(int32_t diff);

/* audio_worker callbacks (run on worker thread). UI mutations must go
 * through scheduler_put() to land on the LVGL task. */
static void on_message_cb(const char *text, int snr, float freq_hz, float time_sec,
                          const slot_info_t *info, void *ctx);
static void on_psd_cb(const float *psd, uint16_t nfft, float sec_since_slot_start,
                      const slot_info_t *info, void *ctx);
static void on_slot_end_cb(const slot_info_t *info, void *ctx);
static void on_tick_cb(const slot_info_t *info, bool new_slot,
                       float sec_since_slot_start, void *ctx);

static const char * cq_all_label_getter();
static const char * protocol_label_getter();
static const char * tx_cq_label_getter();
static const char * tx_call_label_getter();
static const char * hold_freq_label_getter();
static const char * auto_label_getter();

static void show_cq_all_cb(struct button_item_t *btn);
static void mode_ft4_ft8_cb(struct button_item_t *btn);
static void tx_cq_en_dis_cb(struct button_item_t *btn);
static void tx_call_en_dis_cb(struct button_item_t *btn);

static void hold_tx_freq_cb(struct button_item_t *btn);
static void mode_auto_cb(struct button_item_t *btn);
static void cq_modifier_cb(struct button_item_t *btn);
static void time_sync(struct button_item_t *btn);

static void force_save_qso(struct button_item_t *btn);

static void on_table_press(const cell_data_t *cell_data);
static void on_table_close(void);
static void on_table_vol_change(int32_t delta);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close();

static void add_info(const char * fmt, ...);
static void add_tx_text(const char * text);
static bool get_time_slot(struct timespec now, float *time_since_start);

static const char *auto_sel_label_getter(void);
static void auto_sel_cb(struct button_item_t *btn);

// button label is current state, press action and name - next state

static buttons_page_t btn_page_1;
static buttons_page_t btn_page_2;
static buttons_page_t btn_page_3;
static buttons_page_t btn_page_4;

static button_item_t button_page_1 = { .type=BTN_TEXT, .label = "(Page: 1:4)", .press = button_next_page_cb, .next=&btn_page_2};
static button_item_t button_show_cq_all = { .type=BTN_TEXT_FN, .label_fn = cq_all_label_getter, .press = show_cq_all_cb, .subj=&cfg.ft8_show_all.val};
static button_item_t button_auto_sel = { .type=BTN_TEXT_FN, .label_fn = auto_sel_label_getter, .press = auto_sel_cb };
static button_item_t button_mode_ft4_ft8 = { .type=BTN_TEXT_FN, .label_fn = protocol_label_getter, .press = mode_ft4_ft8_cb, .subj=&cfg.ft8_protocol.val };
static button_item_t button_tx_cq_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_cq_label_getter, .press = tx_cq_en_dis_cb };
static button_item_t button_tx_call_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_call_label_getter, .press = tx_call_en_dis_cb};

static button_item_t button_page_2 = { .type=BTN_TEXT, .label = "(Page: 2:4)", .press = button_next_page_cb, .next=&btn_page_3};
static button_item_t button_hold_freq = { .type=BTN_TEXT_FN, .label_fn = hold_freq_label_getter, .press = hold_tx_freq_cb, .subj=&cfg.ft8_hold_freq.val };
static button_item_t button_auto_en_dis = { .type=BTN_TEXT_FN, .label_fn = auto_label_getter, .press = mode_auto_cb, .subj=&cfg.ft8_auto.val };
static button_item_t button_force_save = { .type=BTN_TEXT, .label = "Force QSO\nsave", .press = force_save_qso };

static button_item_t button_page_3 = { .type=BTN_TEXT, .label = "(Page: 3:4)", .press = button_next_page_cb, .next=&btn_page_4};
static button_item_t button_cq_mod = { .type=BTN_TEXT, .label = "CQ\nModifier", .press = cq_modifier_cb };
static button_item_t button_time_sync = { .type=BTN_TEXT, .label = "Time\nSync", .press = time_sync };

static button_item_t button_page_4 = { .type=BTN_TEXT, .label = "(Page: 4:4)", .press = button_next_page_cb, .next=&btn_page_1};

static buttons_page_t btn_page_1 = {
    {&button_page_1, &button_show_cq_all, &button_auto_sel, &button_tx_cq_en_dis, &button_tx_call_en_dis}
};

static buttons_page_t btn_page_2 = {
    {&button_page_2, &button_mode_ft4_ft8, &button_hold_freq, &button_auto_en_dis, &button_force_save}
};

static buttons_page_t btn_page_3 = {
    {&button_page_3, &button_cq_mod, &button_time_sync}
};

static buttons_page_t btn_page_4 = {
    {&button_page_4}
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

/* ---- async LVGL helpers — safe to call from audio worker thread -------- */

static void set_freq(uint32_t freq);

static void finder_set_cursor_cb(void *data) {
    /* The dialog may have been destroyed between scheduling and execution;
     * finder is NULLed in destruct_cb, so a stale item must not touch it. */
    if (!finder) return;
    int16_t freq = *(int16_t *)data;
    lv_finder_set_cursor(finder, freq);
}

static void finder_set_cursor_async(int16_t freq_hz) {
    scheduler_put(finder_set_cursor_cb, &freq_hz, sizeof(freq_hz));
}

static void finder_clear_cursor_cb(void *data) {
    (void)data;
    if (!finder) return;
    lv_finder_clear_cursor(finder);
}

static void finder_clear_cursor_async(void) {
    scheduler_put_noargs(finder_clear_cursor_cb);
}

static void set_freq_async_cb(void *data) {
    if (!finder) return;   /* dialog already destroyed */
    uint32_t freq = *(uint32_t *)data;
    set_freq(freq);
}

static void set_freq_async(uint32_t freq_hz) {
    scheduler_put(set_freq_async_cb, &freq_hz, sizeof(freq_hz));
}

/* cq_enabled is observed by button-label observers whose list is mutated
 * on the LVGL thread (page switch subscribe/unsubscribe). Setting the
 * subject from the worker thread would iterate that list concurrently,
 * so route the write through the scheduler onto the LVGL task. */
static void cq_disable_cb(void *data) {
    (void)data;
    if (cq_enabled) {
        subject_set_int(cq_enabled, false);
    }
}

static void cq_disable_async(void) {
    scheduler_put_noargs(cq_disable_cb);
}

/* ------------------------------------------------------------------------ */

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

    if (strlen(remote_grid) >= 4) {
        double lat, lon, dist;
        qth_str_to_pos(remote_grid, &lat, &lon);
        dist = qth_pos_dist(lat, lon, cur_lat, cur_lon);
        msg_schedule_long_text_fmt("Saved QSO de %s %d %d (%.0f km)", remote_callsign, s_snr, r_snr, dist);
    } else {
        msg_schedule_long_text_fmt("Saved QSO de %s %d %d", remote_callsign, s_snr, r_snr);
    }

    finder_clear_cursor_async();
    autosel_on_qso_saved();
}

static void worker_init() {
    pthread_mutex_lock(&qso_mutex);
    qso_processor = ftx_qso_processor_init(params.callsign.x, params.qth.x,
                                           save_qso,
                                           subject_get_int(cfg.ft8_max_repeats.val));
    pthread_mutex_unlock(&qso_mutex);

    audio_worker_cb_t cb = {
        .on_message  = on_message_cb,
        .on_psd      = on_psd_cb,
        .on_slot_end = on_slot_end_cb,
        .on_tick     = on_tick_cb,
        .ctx         = NULL,
    };
    audio_worker_t *w = audio_worker_create(
        SAMPLE_RATE,
        subject_get_int(cfg.ft8_protocol.val),
        filter_low, filter_high,
        &cb);
    pthread_mutex_lock(&audio_worker_mutex);
    audio_worker = w;
    pthread_mutex_unlock(&audio_worker_mutex);
    if (w) {
        audio_worker_start(w);
    }
}

static void worker_done() {
    state = RX_PROCESS;

    /* Detach the pointer under the mutex first: once audio_worker is NULL,
     * the PulseAudio capture thread (audio_cb) can no longer reach the
     * worker, and any in-flight feed has finished before the lock was
     * granted. Only then is it safe to join and free it. */
    pthread_mutex_lock(&audio_worker_mutex);
    audio_worker_t *w = audio_worker;
    audio_worker = NULL;
    pthread_mutex_unlock(&audio_worker_mutex);
    if (w) {
        audio_worker_destroy(w);
    }
    radio_set_modem(false);

    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_delete(qso_processor);
        qso_processor = NULL;
    }
    pthread_mutex_unlock(&qso_mutex);
    lv_finder_clear_cursor(finder);
    tx_msg.msg[0] = '\0';
}

/* Table widget lifecycle, draw, scroll and message insertion all live
 * in ft8/table_view.c. The dialog only owns the surrounding state. */

static void key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *) lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct(&dialog);
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
    // TODO: check free mem
    keyboard_close();

    /* The 1-shot fade timer from rotary_cb references the table widget;
     * it must not fire after the table is destroyed. */
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    fade_run = false;

    /* Module extension point: cleanup
     * Thread: LVGL / main (destruct_cb runs on dialog close).
     * Timing: AFTER worker_done() — join the audio worker first so
     * on_message / on_psd / on_slot_end / on_tick callbacks cannot touch
     * module state while it is being torn down (UAF / race; see P0-2).
     * Order: reverse of init extension calls if modules depend on each other.
     * Example:
     *   worker_done();
     *   ft8_log_on_cleanup();
     *   ft8_autodnf_on_cleanup();
     *   autosel_cleanup_state(); */

    worker_done();
    autosel_cleanup_state();
    table_view_destroy();

    /* The LVGL objects themselves are deleted by dialog_destruct() via
     * lv_obj_del(dialog.obj). NULL the static handles so stale scheduler
     * items queued by the (already joined) worker thread become no-ops
     * instead of dereferencing freed widgets. */
    finder    = NULL;
    waterfall = NULL;

    pthread_mutex_lock(&psd_mutex);
    psd_staging_len   = 0;
    psd_flush_pending = false;
    pthread_mutex_unlock(&psd_mutex);

    dsp_set_waterfall_enabled(true);
    dsp_set_spectrum_enabled(true);

    mem_load(MEM_BACKUP_ID);

    main_screen_lock_mode(false);
    main_screen_lock_ab(false);
    main_screen_lock_freq(false);
    main_screen_lock_band(false);

    radio_set_pwr(subject_get_float(cfg.pwr.val));
    adif_log_close(ft8_log);
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

    int32_t *c = malloc(sizeof(int32_t));
    *c = LV_KEY_UP;

    lv_event_send(table, LV_EVENT_KEY, c);
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

static void set_freq(uint32_t freq) {
    if (freq > filter_high) {
        freq = filter_high;
    }

    if (freq < filter_low) {
        freq = filter_low;
    }

    params_uint16_set(&params.ft8_tx_freq, freq);

    lv_finder_set_value(finder, freq);
    lv_obj_invalidate(finder);
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

    /* FT8 dialog owns the full screen + audio pipe; main_screen's spectrum
     * and waterfall are not visible, so skip their DSP cost entirely. The
     * companion lv_obj_invalidate() in tx_info handles the side effect of
     * losing the spectrum redraw that previously refreshed PWR/SWR bars. */
    dsp_set_waterfall_enabled(false);
    dsp_set_spectrum_enabled(false);

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

    /* Audio pipeline (cbuffer/firdecim/spgramcf/decoder thread) is created
     * lazily inside worker_init() -> audio_worker_create(). */

    /* Waterfall */

    waterfall = lv_waterfall_create(dialog.obj);

    lv_obj_add_style(waterfall, &waterfall_style, 0);
    lv_obj_clear_flag(waterfall, LV_OBJ_FLAG_SCROLLABLE);

    lv_waterfall_set_palette(waterfall, (lv_color_t*)wf_palette, 256);
    lv_waterfall_set_size(waterfall, WIDTH, 325);
    lv_waterfall_set_min(waterfall, -60);

    lv_obj_set_pos(waterfall, 13, 13);

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

    /* Table */

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

    qth_str_to_pos(params.qth.x, &cur_lat, &cur_lon);

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
    if (x6100_control_get_base_ver().rev >= 3) {
        // patched firmware has a true power control
        base_gain_offset = -9.4f;
    } else {
        base_gain_offset = -16.4f + log10f(target_pwr) * 10.0f;
    }

    /* Module extension point: init
     * Thread: LVGL / main (construct_cb runs on dialog open).
     * Timing: after worker_init() and base gain setup — audio worker and
     * qso_processor are ready; module init may register buttons or load files.
     * Example: ft8_log_on_init(); ft8_autodnf_on_init(); */
    autosel_init_state();
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

const char *auto_sel_label_getter(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "AutoSel:\n%s", autosel_get_mode_text());
    return buf;
}

static void auto_sel_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    autosel_cycle_mode();
    msg_schedule_text_fmt("Auto select: %s", autosel_get_mode_text());
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

    autosel_on_mode_switch();

    worker_done();
    worker_init();
    clean_screen();
    load_band(0);
}

static void mode_auto_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    bool new_val = !subject_get_int(cfg.ft8_auto.val);
    subject_set_int(cfg.ft8_auto.val, new_val);
    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_set_auto(qso_processor, new_val);
    }
    pthread_mutex_unlock(&qso_mutex);
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

        cq_make_message(params.callsign.x, params.qth.x, params.ft8_cq_modifier.x, tx_msg.msg);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        float time_since_slot_start;
        tx_time_slot = !get_time_slot(now, &time_since_slot_start);
        if (time_since_slot_start < MAX_TX_START_DELAY) {
            tx_time_slot = !tx_time_slot;
        }

        if (tx_msg.msg[2] == '_') {
            msg_schedule_text_fmt("Next TX: CQ %s", tx_msg.msg + 3);
        } else {
            msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        }
        tx_msg.repeats = subject_get_int(cfg.ft8_max_repeats.val);
        pthread_mutex_lock(&qso_mutex);
        if (qso_processor) {
            ftx_qso_processor_reset(qso_processor);
        }
        pthread_mutex_unlock(&qso_mutex);
        lv_finder_clear_cursor(finder);
    } else {
        if (state == TX_PROCESS) {
            state = RX_PROCESS;
        }
        subject_set_int(cq_enabled, false);
        tx_msg.msg[0] = '\0';
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
    bool saved = false;
    pthread_mutex_lock(&qso_mutex);
    if (qso_processor && ftx_qso_processor_can_save_qso(qso_processor)) {
        ftx_qso_processor_force_save_qso(qso_processor);
        saved = true;
    }
    pthread_mutex_unlock(&qso_mutex);
    if (!saved) {
        msg_schedule_text_fmt("Can't save incomplete QSO");
    }
}

static void on_table_press(const cell_data_t *cell_data) {
    if (state == TX_PROCESS) {
        tx_call_off();
        return;
    }

    if ((cell_data == NULL) ||
        (cell_data->cell_type == CELL_TX_MSG) ||
        (cell_data->cell_type == CELL_RX_INFO) ||
        (cell_data->cell_type == CELL_START_QSO)
    ) {
        msg_schedule_text_fmt("What should I do about it?");
        return;
    }

    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_start_qso(qso_processor, (ftx_msg_meta_t *)&cell_data->meta, &tx_msg);
    }
    pthread_mutex_unlock(&qso_mutex);
    if (strlen(tx_msg.msg) > 0) {
        lv_finder_set_cursor(finder, cell_data->meta.freq_hz);
        if (!subject_get_int(cfg.ft8_hold_freq.val)) {
            set_freq(cell_data->meta.freq_hz);
        }
        tx_time_slot = !cell_data->odd;
        subject_set_int(tx_enabled, true);
        {
            cell_data_t cd;
            cd.cell_type = CELL_START_QSO;
            snprintf(cd.text, sizeof(cd.text), "Start QSO with %s", cell_data->meta.call_de);
            scheduler_put(table_view_add_msg_cb, &cd, sizeof(cell_data_t));
        }
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
    } else {
        msg_schedule_text_fmt("Invalid message");
        tx_call_off();
    }
}

static void on_table_close(void) {
    dialog_destruct(&dialog);
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

static void audio_cb(unsigned int n, float *samples) {
    pthread_mutex_lock(&audio_worker_mutex);
    if ((state == RX_PROCESS) && audio_worker) {
        audio_worker_feed(audio_worker, n, samples);
    }
    pthread_mutex_unlock(&audio_worker_mutex);
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


/* CQ message composition has moved to ft8/cq_scheduler.c
 * (see cq_make_message()). */

/* TX waveform synthesis + per-block ALC gain correction live in
 * ft8/tx_worker.c. The dialog only owns the per-session abort flag. */
static bool tx_should_abort_cb(void *ctx) {
    (void)ctx;
    return state != TX_PROCESS;
}

/**
 * Add INFO message to the table
 */
static void add_info(const char * fmt, ...) {
    va_list     args;
    cell_data_t  cell_data;
    cell_data.cell_type = CELL_RX_INFO;

    va_start(args, fmt);
    vsnprintf(cell_data.text, sizeof(cell_data.text), fmt, args);
    va_end(args);

    pthread_mutex_lock(&qso_mutex);
    table_view_set_header_collapse(!qso_processor ||
                                   !ftx_qso_processor_has_current(qso_processor));
    pthread_mutex_unlock(&qso_mutex);

    scheduler_put(table_view_add_msg_cb, &cell_data, sizeof(cell_data_t));
}

/**
 * Add TX message to the table
 */
static void add_tx_text(const char * text) {
    cell_data_t  cell_data;
    cell_data.cell_type = CELL_TX_MSG;

    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    if (strncmp(cell_data.text, "CQ_", 3) == 0) {
        cell_data.text[2] = ' ';
    }
    scheduler_put(table_view_add_msg_cb, &cell_data, sizeof(cell_data_t));
}

/**
 * Parse and add RX messages to the table
 */
static void add_rx_text(int16_t snr, const char * text, slot_info_t *s_info, float freq_hz, float time_sec) {

    ftx_msg_meta_t *meta = &last_rx_meta;
    memset(meta, 0, sizeof(*meta));
    meta->freq_hz = freq_hz;
    meta->time_sec = time_sec;
    char * old_msg = strdup(tx_msg.msg);
    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_add_rx_text(qso_processor, text, snr, meta, &tx_msg);
    }
    pthread_mutex_unlock(&qso_mutex);

    if ((strlen(tx_msg.msg) > 0) && (strcmp(old_msg, tx_msg.msg) != 0)) {
        finder_set_cursor_async(meta->freq_hz);
        if (!subject_get_int(cfg.ft8_hold_freq.val)) {
            set_freq_async(freq_hz);
        }
        tx_time_slot = !s_info->odd;
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        autosel_on_tx_msg_updated(meta, s_info->odd);
        if (subject_get_int(cq_enabled)) {
            cq_disable_async();
        }
    }
    free(old_msg);

    ft8_cell_type_t cell_type;
    if (meta->to_me) {
        cell_type = CELL_RX_TO_ME;
    } else if (meta->type == FTX_MSG_TYPE_CQ) {
        cell_type = CELL_RX_CQ;
    } else if (!subject_get_int(cfg.ft8_show_all.val)) {
        return;
    } else {
        cell_type = CELL_RX_MSG;
    }

    cell_data_t  cell_data;
    if (meta->type == FTX_MSG_TYPE_CQ) {
        cell_data.worked_type = qso_log_search_worked(
            meta->call_de,
            subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
            qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq))
        );
    }

    cell_data.cell_type = cell_type;
    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    cell_data.meta = *meta;
    cell_data.odd = s_info->odd;
    if (params.qth.x[0] != 0) {
        if (strlen(meta->grid) > 0) {
            double lat, lon;
            qth_str_to_pos(meta->grid, &lat, &lon);
            cell_data.dist = qth_pos_dist(lat, lon, cur_lat, cur_lon);
        } else {
            cell_data.dist = 0;
        }
    } else {
        cell_data.dist = 0;
    }
    scheduler_put(table_view_add_msg_cb, (void*)&cell_data, sizeof(cell_data_t));
}

/* ---- audio_worker callbacks (run on the worker thread) ---------------- */

static void on_message_cb(const char *text, int snr, float freq_hz, float time_sec,
                          const slot_info_t *info, void *ctx) {
    (void)ctx;
    add_rx_text((int16_t)snr, text, (slot_info_t *)info, freq_hz, time_sec);

    /* Module extension point: rx_msg
     * Thread: audio worker (same as this callback).
     * Timing: immediately after add_rx_text() — last_rx_meta and info are
     * valid; tx_msg may have been updated by qso_processor inside add_rx_text.
     * Constraint: no direct lv_* calls; use scheduler_put / *_async helpers.
     * Example: ft8_log_on_rx_msg(text, snr, freq_hz, time_sec, &last_rx_meta, info); */
    autosel_rx_hook(text, snr, freq_hz, time_sec, &last_rx_meta, info);
}

/*
 * Coalesced waterfall flush — runs on the LVGL task via scheduler.
 * Drains psd_staging[] and emits a single lv_waterfall_add_data call.
 */
static void flush_ft8_waterfall_cb(void *arg) {
    (void)arg;

    float  local_psd[PSD_STAGING_MAX];
    size_t local_len;

    pthread_mutex_lock(&psd_mutex);
    local_len = psd_staging_len;
    if (local_len > 0) {
        memcpy(local_psd, psd_staging, local_len * sizeof(float));
    }
    psd_staging_len   = 0;
    psd_flush_pending = false;
    pthread_mutex_unlock(&psd_mutex);

    if ((local_len > 0) && waterfall) {
        lv_waterfall_add_data(waterfall, local_psd, local_len);
    }
}

static void on_psd_cb(const float *psd, uint16_t nfft, float sec_since_slot_start,
                      const slot_info_t *info, void *ctx) {
    (void)sec_since_slot_start;
    (void)info;
    (void)ctx;
    if (!psd || !nfft) return;

    uint32_t low_bin  = (uint32_t)nfft / 2u + (uint32_t)nfft * filter_low  / SAMPLE_RATE;
    uint32_t high_bin = (uint32_t)nfft / 2u + (uint32_t)nfft * filter_high / SAMPLE_RATE;
    if (high_bin > nfft) high_bin = nfft;
    if (low_bin >= high_bin) return;

    size_t len = high_bin - low_bin;
    if (len > PSD_STAGING_MAX) len = PSD_STAGING_MAX;

    /* Write latest PSD row into staging; schedule exactly one flush
     * when none is pending.  The LVGL task drains staging later. */
    pthread_mutex_lock(&psd_mutex);
    memcpy(psd_staging, &psd[low_bin], len * sizeof(float));
    psd_staging_len = len;

    bool need_flush = !psd_flush_pending;
    if (need_flush) {
        psd_flush_pending = true;
    }
    pthread_mutex_unlock(&psd_mutex);

    if (need_flush && !scheduler_put_noargs(flush_ft8_waterfall_cb)) {
        /* Flush item dropped (queue overflow): roll the flag back so a
         * later PSD frame can retry, otherwise the waterfall would freeze
         * with psd_flush_pending stuck at true. */
        pthread_mutex_lock(&psd_mutex);
        psd_flush_pending = false;
        pthread_mutex_unlock(&psd_mutex);
    }

    /* Module extension point: psd
     * Thread: audio worker (same as this callback).
     * Timing: after core waterfall staging is queued — psd[] and filter bins
     * are valid; runs once per emitted PSD frame (~10 Hz).
     * Constraint: no direct lv_* / lv_waterfall_*; use scheduler_put only.
     * Example: ft8_autodnf_on_psd(psd, nfft, sec_since_slot_start, info); */
}

static void on_slot_end_cb(const slot_info_t *info, void *ctx) {
    (void)ctx;
    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_start_new_slot(qso_processor);
    }
    pthread_mutex_unlock(&qso_mutex);

    /* Module extension point: slot_end
     * Thread: audio worker (same as this callback).
     * Timing: at FT8/FT4 slot boundary, after ftx_qso_processor_start_new_slot()
     * and final decode flush — info describes the slot that just ended.
     * Constraint: no direct lv_* calls; use scheduler_put / *_async helpers.
     * Example: ft8_log_on_slot_end(info); ft8_autosel_on_slot_end(info); */
    autosel_slot_end_hook(info);
}

static void on_tick_cb(const slot_info_t *info, bool new_slot,
                       float sec_since_slot_start, void *ctx) {
    (void)ctx;

    bool have_tx_msg = tx_msg.msg[0] != '\0';

    if ((sec_since_slot_start < MAX_TX_START_DELAY) && have_tx_msg) {
        if ((tx_time_slot == info->odd) && subject_get_int(tx_enabled)) {

            /* Module extension point: pre_tx
             * Thread: audio worker (on_tick_cb).
             * Timing: sec_since_slot_start < MAX_TX_START_DELAY, tx_time_slot
             * matches info->odd, TX enabled, and tx_msg non-empty — immediately
             * before state = TX_PROCESS and tx_worker_run_with_config().
             * Use for: TX file log open, DNF marker clear, grid-swap on tx_msg.
             * Cannot defer TX from here without modifying core flow below.
             * Example: ft8_log_on_pre_tx(info); */
            autosel_grid_swap_on_tick(info);
            /* continue TX this tick — no slot defer */

            state = TX_PROCESS;

            ft8_tx_config_t tx_cfg = {
                .tx_text          = tx_msg.msg,
                .base_gain_offset = base_gain_offset,
                .abort_check      = tx_should_abort_cb,
                .abort_check_ctx  = NULL,
            };
            add_tx_text(tx_msg.msg);
            tx_worker_run_with_config(&tx_cfg);
            state = RX_PROCESS;

            /* Module extension point: post_tx
             * Thread: audio worker (on_tick_cb).
             * Timing: immediately after tx_worker_run_with_config() returns —
             * TX slot finished; tx_msg.repeats not yet decremented.
             * Example: ft8_autosel_on_post_tx(info); */

            if (tx_msg.repeats > 0) {
                tx_msg.repeats--;
            }
            autosel_post_tx();
            if (tx_msg.repeats == 0) {
                if (strncmp(tx_msg.msg, "CQ", 2) == 0) {
                    cq_disable_async();
                }
                tx_msg.msg[0] = '\0';
            }
            return;
        }
    }

    if (new_slot) {
        state = RX_PROCESS;
        if (!have_tx_msg || !subject_get_int(tx_enabled)) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            struct tm *ts = localtime(&now.tv_sec);
            add_info("RX %s %02i:%02i:%02i", cfg_digital_label_get(),
                     ts->tm_hour, ts->tm_min, ts->tm_sec);
        }
    }
}

/* ---- Deferred API for AutoSel module (implemented here, declared in auto_sel.h) */

FTxQsoProcessor *ft8_get_qso_processor(void) { return qso_processor; }
ftx_tx_msg_t    *ft8_get_tx_msg(void)        { return &tx_msg; }
bool            *ft8_get_tx_time_slot(void)   { return &tx_time_slot; }
lv_obj_t        *ft8_get_finder(void)         { return finder; }
lv_obj_t        *ft8_get_waterfall(void)      { return waterfall; }
bool             ft8_is_tx_enabled(void)      { return subject_get_int(tx_enabled); }
bool             ft8_is_cq_enabled(void)      { return subject_get_int(cq_enabled); }
void             ft8_set_cq_enabled(bool on)  { subject_set_int(cq_enabled, on); }

void ft8_get_qth(double *lat, double *lon) {
    if (lat) *lat = cur_lat;
    if (lon) *lon = cur_lon;
}

void ft8_set_dial_freq(uint32_t freq) {
    set_freq(freq);
}

void ft8_finder_set_cursor_async(int16_t freq_hz) {
    finder_set_cursor_async(freq_hz);
}

void ft8_set_dial_freq_async(uint32_t freq) {
    set_freq_async(freq);
}

void ft8_schedule_cq_tx(void) {
    if (strlen(params.callsign.x) == 0)
        return;

    cq_make_message(params.callsign.x, params.qth.x,
                    params.ft8_cq_modifier.x, tx_msg.msg);

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    float time_since_slot_start;
    tx_time_slot = !get_time_slot(now, &time_since_slot_start);
    if (time_since_slot_start < MAX_TX_START_DELAY) {
        tx_time_slot = !tx_time_slot;
    }
    tx_msg.repeats = subject_get_int(cfg.ft8_max_repeats.val);
    subject_set_int(tx_enabled, true);
    subject_set_int(cq_enabled, true);
    pthread_mutex_lock(&qso_mutex);
    if (qso_processor) {
        ftx_qso_processor_reset(qso_processor);
    }
    pthread_mutex_unlock(&qso_mutex);
    finder_clear_cursor_async();

    if (tx_msg.msg[2] == '_') {
        msg_schedule_text_fmt("Next TX: CQ %s", tx_msg.msg + 3);
    } else {
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
    }
}

static void ft8_schedule_cq_tx_cb(void *data) {
    (void)data;
    ft8_schedule_cq_tx();
}

void ft8_schedule_cq_tx_async(void) {
    scheduler_put_noargs(ft8_schedule_cq_tx_cb);
}
