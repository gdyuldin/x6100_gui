/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * Additional term under section 7(b) of the GNU GPL version 3: any work that
 * contains this file, or a work based on it, must preserve the author
 * attribution "Hany El Imam" and the credits of the WEFAX, NAVTEX/SITOR-B,
 * DSC and SSTV applications in the Appropriate Legal Notices displayed by
 * its user interface (in this program: the Info window of each application).
 * See NOTICE-1KO125.md.
 */
/*
 * Xiegu X6100 SSTV app
 *
 * (c) 2026 Hany El Imam
 * www.hanynet.com - www.murusfirewall.com - https://github.com/TheMurusTeam
 *
 */

#include "dialog_sstv.h"
#include "styles.h"
#include "dialog.h"
#include "buttons.h"
#include "main_screen.h"
#include "keyboard.h"
#include "textarea_window.h"
#include "sstv/decoder.h"
#include "sstv/encoder.h"
#include "sstv/autodetect.h"
#include "sstv/vis.h"
#include "sstv/linesync.h"
#include "sstv/preview.h"
#include "radio.h"
#include "tx_info.h"
#include "events.h"
#include "msg.h"
#include "ui_list.h"
#include "app_info.h"
#include "cfg/cfg_api.h"

#include "dsp.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <png.h>

#include "audio.h"

#define SSTV_CANVAS_W 440
#define SSTV_CANVAS_H 330
#define SSTV_TEST_WAV "/mnt/sstv.wav"

/* Third menu page [SSTV 3:3]: Info (F4) always; Test RX (F2) and AUTODETECT
 * (F3) only with SSTV_PAGE3_MENU 1.  On page 3 the AUTODETECT button is shown
 * only with SSTV_PAGE3_AUTODETECT 1 (its code is kept).
 * Test RX disabled for now: set SSTV_PAGE3_MENU back to 1 to get it again. */
#define SSTV_PAGE3_MENU 0
#define SSTV_PAGE3_AUTODETECT 0
#define SSTV_DEBUG_WAV "/mnt/sstv-debug.wav"
/* Save every Auto-RX capture to SSTV_DEBUG_WAV before decoding.
 * Disabled: 5-13 MB written to the SD card per picture (wear + delay).
 * Set to 1 to collect real recordings for debugging/regression tests. */
#define SSTV_SAVE_DEBUG_WAV 0
#define SSTV_SAVE_DIR "/mnt/sstv"
#define SSTV_IMAGE_H 330
#define SSTV_IMAGE_W 440
#define SSTV_IMAGE_X 350
#define SSTV_IMAGE_Y 10
#define SSTV_BROWSER_W 710
#define SSTV_BROWSER_H 320
#define MAX_PWR 5.0f

/* Interactive TX picture selector. */
#define SSTV_TX_PICS_DIR "/mnt/sstv/tx_pics"
#define SSTV_TX_TEXT_DIR "/mnt/sstv/tx_text"
#define SSTV_TX_PICTURE_DEBOUNCE_MS 300
#define SSTV_TX_LIST_X 10
#define SSTV_TX_LIST_Y 118
#define SSTV_TX_LIST_W 300
#define SSTV_TX_LIST_H 200
/* Image position is calculated from the mode detected by VIS. */

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
#if SSTV_PAGE3_MENU
static void test_cb(button_data_t *data);
#endif
static void close_cb(button_data_t *data);
static void restart_cb(button_data_t *data);
static void key_cb(lv_event_t *e);
static void start_cb(button_data_t *data);
static void cont_start_cb(button_data_t *data);
static void cont_mode_cb(button_data_t *data);
static void browser_cb(button_data_t *data);
static void delete_picture_cb(button_data_t *data);
static void page_1_cb(button_data_t *data);
static void page_2_cb(button_data_t *data);
static void page_3_cb(button_data_t *data);
static void info_cb(button_data_t *data);   /* 1KO125: Info window, see app_info.c */
#if SSTV_PAGE3_MENU
#if SSTV_PAGE3_AUTODETECT
static void autodetect_cb(button_data_t *data);
#endif
#endif
static void audio_cb(size_t n, float *samples);
static void tx_ui_cb(button_data_t *data);
static void tx_picture_selector_build(void);
static void tx_picture_selector_destroy(void);
static void tx_picture_selector_refresh(void);
static void tx_picture_selection_changed_cb(lv_event_t *e);
static void tx_picture_activate_cb(void *user, int row);
static void tx_picture_schedule_preview(void);
static void tx_picture_load_pending_preview(void);
static bool tx_picture_load_and_prepare(const char *filename);
static void tx_picture_render_preview(void);
static void tx_save_rx_canvas(void);
static void tx_restore_rx_canvas(void);
static void tx_rebuild_current_overlay(void);
static void tx_type_selector_build(void);
static void tx_type_selector_destroy(void);
static void tx_type_activate_cb(void *user, int row);
static void tx_type_selection_changed_cb(lv_event_t *e);
static void tx_type_update_help(void);
static void tx_instruction_show(const char *text);
static void tx_instruction_hide(void);
static void tx_build_final_frame(bool cq_overlay);
static void tx_template_selector_build(void);
static void tx_template_selector_destroy(void);
static void tx_template_selection_changed_cb(lv_event_t *e);
static void tx_template_activate_cb(void *user, int row);
static void tx_template_render(void);
static void tx_template_render_selected(void);
static void tx_target_selector_build(void);
static void tx_target_selector_destroy(void);
static void tx_target_activate_cb(void *user, int row);
static bool tx_target_keyboard_ok_cb(void);
static bool tx_target_keyboard_cancel_cb(void);
static void tx_target_render(void);
static lv_obj_t *tx_find_keyboard_recursive(lv_obj_t *parent);
static void tx_target_keyboard_force_lower(void);
static void tx_target_keyboard_open_now(void);
static void tx_color_selector_build(void);
static void tx_color_selector_destroy(void);
static void tx_color_selection_changed_cb(lv_event_t *e);
static void tx_color_activate_cb(void *user, int index);
static void tx_color_render_selected(void);
static void ui_timer_cb(lv_timer_t *timer);
#if SSTV_PAGE3_MENU
static void *test_thread_cb(void *arg);
#endif
static void *live_decode_thread_cb(void *arg);
static void *cont_decode_thread_cb(void *arg);
static void *tx_thread_cb(void *arg);
static bool save_debug_wav(const char *path, const float *samples, long count);
static bool save_sstv_png(const uint8_t *rgb, int width, int height, char *saved_name, size_t saved_name_size);
static bool save_tx_png(const char *path, const uint8_t *rgb, unsigned int width, unsigned int height);
static bool tx_start_final_rf(void);
static bool tx_start_final_rf_mode(sstv_mode_t mode);
static void tx_return_to_auto_rx(void);
static void set_status(const char *text);
static void reset_ui_to_standby(void);
static void render_scaled_image(void);
static button_data_t btn_sstv_page_1;
static button_data_t btn_start;
static button_data_t btn_cont_start;
static button_data_t btn_cont_mode;
static button_data_t btn_browser;
static button_data_t btn_delete_picture;
static button_data_t btn_tx;
static buttons_page_t btn_page_1;

static void set_receiver_ui_stopped(void);
static void set_receiver_ui_listening(void);
static void set_receiver_ui_decoding(const char *mode_name);

static uint32_t dsp_audio_sub_id = AUDIO_SUB_INVALID;
static lv_obj_t *sstv_frame = NULL;
static lv_obj_t *sstv_canvas = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *progress_bar = NULL;
static volatile bool saved_overlay_pending = false;

/* Valid-but-unsupported VIS notification.
 * VIS DSP runs from ui_timer_cb(), but keep the detector independent from
 * LVGL: it only queues an event; the timer displays the system popup. */
static bool unsupported_vis_pending = false;
static int unsupported_vis_code = -1;
static char unsupported_vis_name[48];
static sstv_autodetect_state_t autodetect_last_ui_state = SSTV_AUTODETECT_IDLE;
static char saved_overlay_name[96];
static lv_color_t sstv_buffer[SSTV_CANVAS_W * SSTV_CANVAS_H];
static uint8_t test_rgb[SSTV_MAX_W * SSTV_MAX_H * 3];
static sstv_info_t test_info;
static lv_timer_t *ui_timer = NULL;
static pthread_t test_thread;
static bool test_thread_valid = false;
static volatile bool test_running = false;
static volatile bool test_ready = false;
static volatile int test_rows = 0;
static int shown_rows = 0;

/* SSTV test transmitter.  The worker owns all blocking audio/radio work;
 * LVGL is touched only by ui_timer_cb(). */
static pthread_t tx_thread;
static bool tx_thread_valid = false;
static volatile bool tx_running = false;
static volatile bool tx_abort = false;
static volatile bool tx_started = false;
static volatile int tx_progress = 0;
static sstv_mode_t tx_mode = SSTV_MODE_ROBOT8_BW;
static struct timespec tx_key_time;
static volatile bool tx_key_time_valid = false;

/* Nominal on-air SSTV payload durations.  Progress is deliberately based on
 * elapsed wall-clock time, not on how quickly audio_play() accepts samples. */
#define SSTV_TX_PREROLL_MS 500u
static volatile int tx_result = 0; /* 0 idle/running, 1 done, <0 error/abort */

/* New interactive TX UI state.  Step 1 only implements entry/cancel; the
 * picture/template phases are added on top of this state machine. */
typedef enum { SSTV_TX_UI_RX = 0, SSTV_TX_UI_SETUP, SSTV_TX_UI_RF } sstv_tx_ui_state_t;
static sstv_tx_ui_state_t tx_ui_state = SSTV_TX_UI_RX;
static bool tx_restore_auto_rx = false;
static char tx_mode_label[48];

typedef enum {
    SSTV_TX_PHASE_NONE = 0,
    SSTV_TX_PHASE_SELECT_PICTURE,
    SSTV_TX_PHASE_SELECT_TYPE,
    SSTV_TX_PHASE_SELECT_TEMPLATE,
    SSTV_TX_PHASE_SELECT_TARGET,
    SSTV_TX_PHASE_SELECT_COLORS
} sstv_tx_phase_t;

static sstv_tx_phase_t tx_phase = SSTV_TX_PHASE_NONE;
static bool tx_type_preview_cq = true;
static bool tx_template_preview_pending = false;
static bool tx_color_preview_pending = false;
static uint32_t tx_color_preview_since_ms = 0;
static bool tx_target_keyboard_open_pending = false;
static uint32_t tx_target_keyboard_force_until = 0;
typedef enum {
    SSTV_TX_MESSAGE_CQ = 0,
    SSTV_TX_MESSAGE_REPLY,
    SSTV_TX_MESSAGE_REPLAY,
    SSTV_TX_MESSAGE_IMAGE_ONLY,
    SSTV_TX_MESSAGE_REPEAT_LAST
} sstv_tx_message_type_t;
static char **tx_picture_files = NULL;
static size_t tx_picture_file_count = 0;
static int tx_picture_selected = -1;
static uint8_t tx_final_rgb[320 * 256 * 3];
static unsigned int tx_picture_width = 0;
static unsigned int tx_picture_height = 0;

/* Runtime-only backup of the last SSTV transmission that completed
 * successfully.  Store the exact transmitted pixels plus the mode so
 * REPEAT LAST TX is independent of the currently selected TX Mode. */
static uint8_t tx_last_rgb[320 * 256 * 3];
static unsigned int tx_last_width = 0;
static unsigned int tx_last_height = 0;
static sstv_mode_t tx_last_mode = SSTV_MODE_SCOTTIE_S1;
static bool tx_last_valid = false;
static bool sstv_browser_open = false;
static bool sstv_browser_preview = false;

static void sstv_browser_free_files(void);
static void sstv_browser_refresh(void);
static void sstv_browser_update_list(void);
static void sstv_browser_build(void);
static void sstv_browser_close(void);
static void sstv_browser_return_to_list(void);
static void sstv_browser_delete_selected(void);
static bool sstv_browser_open_png(const char *filename);
static void sstv_browser_free_image(void);
static void sstv_browser_render_image(void);
static void sstv_browser_preview_pressed_cb(lv_event_t *e);
static void sstv_browser_update_buttons(void);

/* Live RF capture.  Keep a short rolling prebuffer so the VIS leader is
 * preserved when we decide that an SSTV transmission has started. */
#define SSTV_PREBUF_SAMPLES (SSTV_SAMPLE_RATE * 2)
#define SSTV_VIS_WINDOW_SAMPLES (SSTV_SAMPLE_RATE)
#define SSTV_VIS_CHECK_STEP (SSTV_SAMPLE_RATE / 20) /* 50 ms */
/* VIS detector: 0 = sstv/vis.c (default), 1 = previous live_find_vis(),
 * kept for comparison during the beta. */
#define SSTV_USE_LEGACY_VIS 0

static pthread_mutex_t live_mutex = PTHREAD_MUTEX_INITIALIZER;
static float live_ring[SSTV_PREBUF_SAMPLES];
static float live_vis_window[SSTV_VIS_WINDOW_SAMPLES];
static size_t live_ring_pos = 0;
static size_t live_ring_count = 0;
/* VIS scan scheduling.  live_ring_total counts every sample ever written to
 * live_ring (never reset).  Each detector analyses the 1-s window ending at
 * its own "next end" and then advances it by exactly SSTV_VIS_CHECK_STEP, so
 * consecutive windows are always 50 ms of audio apart, whatever the audio
 * block size or GUI timer jitter (see live_vis_window_ready()). */
static uint64_t live_ring_total = 0;
static uint64_t live_scan_next_end = 0;
static uint64_t live_restart_next_end = 0;
static uint64_t live_wait_samples = 0;
static uint64_t live_wait_log_mark = 0;
static volatile bool live_enabled = false;
static volatile bool live_capturing = false;
static volatile bool live_decode_pending = false;
static volatile bool live_decoding = false;
static volatile bool live_canvas_clear_pending = false;
static volatile bool live_abort_requested = false;
static volatile bool live_mode_ui_pending = false;
static volatile bool live_restart_enabled = true;
/* Progressive display (Auto RX).  live_ls follows the line syncs of the
 * capture (fed by audio_cb() under live_mutex); live_pv decides which row
 * can be drawn; ui_timer_cb() draws up to SSTV_PV_ROWS_PER_TICK rows per
 * tick.  The final decode replaces the preview when the capture ends. */
#define SSTV_PV_ROWS_PER_TICK 2
#define SSTV_PV_SLICE_MAX (SSTV_SAMPLE_RATE * 22 / 10)   /* > 1.9 Scottie DX lines + margins */
static sstv_linesync_t live_ls;
static sstv_preview_t live_pv;
static bool live_pv_on = false;
static float live_pv_slice[SSTV_PV_SLICE_MAX];
static float live_vis_last_start_s = -1.0f;   /* VIS start in the last detected window */
static float *live_samples = NULL;
static long live_written = 0;
static long live_target = 0;
static sstv_info_t live_info;

/* Temporary live-RX diagnostics. No SSTV behaviour is changed here. */
static struct timespec live_dbg_capture_ts;
static struct timespec live_dbg_listen_ts;
static bool live_dbg_listen_ts_valid = false;
static uint64_t live_dbg_vis_checks = 0;
static uint64_t live_dbg_vis_check_samples = 0;

/* Manual continuous RX. This state machine is deliberately separate from
 * Auto RX so the validated VIS-driven path remains unchanged. */
#define SSTV_CONT_MAX_SECONDS 300
static volatile bool cont_capturing = false;
static volatile bool cont_decode_pending = false;
static volatile bool cont_decoding = false;
static float *cont_samples = NULL;
static long cont_written = 0;
static long cont_capacity = 0;
static sstv_mode_t cont_mode = SSTV_MODE_ROBOT8_BW;
static sstv_mode_t cont_decode_mode = SSTV_MODE_ROBOT8_BW;

static const sstv_mode_t cont_modes[] = {
    SSTV_MODE_ROBOT8_BW, SSTV_MODE_ROBOT36, SSTV_MODE_ROBOT72,
    SSTV_MODE_MARTIN_M1, SSTV_MODE_MARTIN_M2,
    SSTV_MODE_SCOTTIE_S1, SSTV_MODE_SCOTTIE_S2
};

/* Manual RX mode selection.  Kept apart from cont_mode, which is also the
 * TX mode: SSTV_MODE_UNKNOWN means "Autodetect", and Scottie DX can be
 * received but is never transmitted. */
static sstv_mode_t cont_rx_mode = SSTV_MODE_UNKNOWN;
static const sstv_mode_t cont_rx_modes[] = {
    SSTV_MODE_UNKNOWN,
    SSTV_MODE_ROBOT8_BW, SSTV_MODE_ROBOT36, SSTV_MODE_ROBOT72,
    SSTV_MODE_MARTIN_M1, SSTV_MODE_MARTIN_M2,
    SSTV_MODE_SCOTTIE_S1, SSTV_MODE_SCOTTIE_S2, SSTV_MODE_SCOTTIE_DX
};
/* Manual RX line-sync tracker (sstv/linesync.c), fed by audio_cb() under
 * live_mutex.  It recognises the mode (Autodetect), closes the capture when
 * the syncs are gone, and the capture is also limited to the full duration
 * of the mode counted from Start. */
static sstv_linesync_t cont_ls;
/* Progressive display for Manual RX (cont_preview_tick()): rows start at the
 * top from the first complete line after Start, as in the final decode. */
static sstv_preview_t cont_pv;
static bool cont_pv_on = false;
static long cont_limit = 0;                       /* samples              */
static bool cont_locked = false;
static sstv_mode_t cont_detected = SSTV_MODE_UNKNOWN;
typedef enum {
    SSTV_CONT_EV_NONE = 0, SSTV_CONT_EV_LOCK, SSTV_CONT_EV_END_SYNC,
    SSTV_CONT_EV_END_LIMIT, SSTV_CONT_EV_END_VIS,
    SSTV_CONT_EV_NO_LOCK, SSTV_CONT_EV_MISMATCH, SSTV_CONT_EV_NO_SIGNAL,
    SSTV_CONT_EV_DETECT_TIMEOUT
} sstv_cont_event_t;
/* Autodetect: if no mode is recognised within this time, a last attempt is
 * made on the audio captured so far; if that fails too, Manual RX stops. */
#define SSTV_CONT_DETECT_SECONDS 10
static bool cont_timeout_checked = false;
static volatile sstv_mode_t cont_class_mode = SSTV_MODE_UNKNOWN;  /* a-posteriori */
static volatile bool cont_no_signal = false;  /* last Manual decode found nothing */
static volatile sstv_cont_event_t cont_event = SSTV_CONT_EV_NONE;  /* -> ui_timer_cb */
static volatile sstv_cont_event_t cont_end_reason = SSTV_CONT_EV_NONE;

/* 3-page menu (Info, and Test RX / AUTODETECT if enabled, on page 3). */
static button_data_t btn_sstv_page_1 = {
    .type = BTN_TEXT, .label = "(SSTV 1:3)", .press = page_2_cb
};
static button_data_t btn_sstv_page_2 = {
    .type = BTN_TEXT, .label = "(SSTV 2:3)", .press = page_3_cb
};
static button_data_t btn_sstv_page_3 = {
    .type = BTN_TEXT, .label = "(SSTV 3:3)", .press = page_1_cb
};
static button_data_t btn_start = { .type = BTN_TEXT, .label = "Start\nAuto RX", .press = start_cb };
static button_data_t btn_cont_start = { .type = BTN_TEXT, .label = "Start\nManual RX", .press = cont_start_cb };
static button_data_t btn_cont_mode = { .type = BTN_TEXT, .label = "RX Mode\nAutodetect", .press = cont_mode_cb };
static button_data_t btn_browser = { .type = BTN_TEXT, .label = "RX Pics\nBrowser", .press = browser_cb };
static button_data_t btn_delete_picture = { .type = BTN_TEXT, .label = "Delete\npicture", .press = delete_picture_cb };
static button_data_t btn_tx = { .type = BTN_TEXT, .label = "TX", .press = tx_ui_cb };
static button_data_t btn_restart = { .type = BTN_TEXT, .label = "Auto RX\nRestart ON", .press = restart_cb };
#if SSTV_PAGE3_MENU
static button_data_t btn_test = { .type = BTN_TEXT, .label = "Test RX", .press = test_cb };
#if SSTV_PAGE3_AUTODETECT
static button_data_t btn_autodetect = { .type = BTN_TEXT, .label = "AUTODETECT", .press = autodetect_cb };
#endif
#endif
static button_data_t btn_close = { .type = BTN_TEXT, .label = "Close\nSSTV", .press = close_cb };
static button_data_t btn_info = { .type = BTN_TEXT, .label = "Info", .press = info_cb };

static buttons_page_t btn_page_1 = {
    .items = { &btn_sstv_page_1, &btn_start, &btn_cont_start, &btn_cont_mode, &btn_tx }
};
static buttons_page_t btn_page_2 = {
    .items = { &btn_sstv_page_2, &btn_browser, &btn_delete_picture, &btn_restart, &btn_close }
};
static buttons_page_t btn_page_3 = {
#if SSTV_PAGE3_MENU && SSTV_PAGE3_AUTODETECT
    .items = { &btn_sstv_page_3, &btn_test, &btn_autodetect, &btn_info, NULL }
#elif SSTV_PAGE3_MENU
    .items = { &btn_sstv_page_3, &btn_test, NULL, &btn_info, NULL }
#else
    .items = { &btn_sstv_page_3, NULL, NULL, &btn_info, NULL }
#endif
};

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .key_cb = key_cb,
    .btn_page = &btn_page_1
};

dialog_t *dialog_sstv = &dialog;

static void info_cb(button_data_t *data) { (void)data; app_info_open(dialog.obj, APP_INFO_SSTV); }

#define TX_RGB(R,G,B) { (R),(G),(B) }

static void update_rx_controls(void)
{
    bool auto_active = live_enabled || live_capturing || live_decode_pending || live_decoding;
    bool cont_active = cont_capturing || cont_decode_pending || cont_decoding;

    /* F1 changes SSTV soft-key page.  Keep it locked for the whole TX
     * workflow (setup and RF) so the operator cannot leave the TX controls.
     * Every path that returns to RX calls update_rx_controls(), which enables
     * F1 again automatically. */
    buttons_disabled(&btn_sstv_page_1, tx_ui_state != SSTV_TX_UI_RX);

    if (tx_ui_state != SSTV_TX_UI_RX) {
        buttons_disabled(&btn_start, true);
        buttons_disabled(&btn_cont_start, true);
        buttons_disabled(&btn_cont_mode, tx_ui_state == SSTV_TX_UI_RF);
        btn_page_1.items[3] = &btn_cont_mode;
        if (buttons_get_cur_page() == &btn_page_1) buttons_load_page(&btn_page_1);
        return;
    }

    buttons_disabled(&btn_start, cont_active);
    buttons_disabled(&btn_cont_start, auto_active || cont_decode_pending || cont_decoding);
    buttons_disabled(&btn_cont_mode, auto_active || cont_active);

    /* The mode selector is only present while both receivers are idle. */
    btn_page_1.items[3] = (!auto_active && !cont_active) ? &btn_cont_mode : NULL;
    if (buttons_get_cur_page() == &btn_page_1)
        buttons_load_page(&btn_page_1);
}

static void set_status(const char *text)
{
    if (status_label) lv_label_set_text(status_label, text ? text : "");
}

static void set_receiver_ui_stopped(void)
{
    set_status("Stand by.\n\n- Press Start Auto RX to start Autodecoder.\n\n- Press Start Manual RX to start Manual decoder, press RX Mode button to cycle all available modes for the Manual decoder.");
    if (progress_bar) {
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    btn_start.label = "Start\nAuto RX";
    buttons_disabled(&btn_start, false);
    if (btn_start.disp_btn) buttons_refresh(&btn_start);
    update_rx_controls();
}

static void set_receiver_ui_listening(void)
{
    set_status("SSTV Autodecoder is running.\n\nAwaiting signal...");
    if (progress_bar) {
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    btn_start.label = "Stop\nAuto RX";
    buttons_disabled(&btn_start, false);
    if (btn_start.disp_btn) buttons_refresh(&btn_start);
    update_rx_controls();
}

static void set_receiver_ui_decoding(const char *mode_name)
{
    char status[96];
    snprintf(status, sizeof(status), "Decoding %s", mode_name ? mode_name : "SSTV");
    set_status(status);
    if (progress_bar) lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    btn_start.label = "Halt\nDecoding";
    buttons_disabled(&btn_start, false);
    if (btn_start.disp_btn) buttons_refresh(&btn_start);
    update_rx_controls();
}

static void reset_ui_to_standby(void)
{
    set_receiver_ui_stopped();
}

static void render_scaled_image(void)
{
    if (!sstv_canvas || test_rows <= 0 || test_info.width <= 0 || test_info.height <= 0) return;
    lv_canvas_fill_bg(sstv_canvas, lv_color_white(), LV_OPA_COVER);
    int out_h=SSTV_IMAGE_H;
    int out_w=(test_info.width*out_h)/test_info.height;
    if(out_w>SSTV_IMAGE_W){out_w=SSTV_IMAGE_W;out_h=(test_info.height*out_w)/test_info.width;}
    int x0=(SSTV_IMAGE_W-out_w)/2;
    int y0=(SSTV_IMAGE_H-out_h)/2;
    for(int oy=0;oy<out_h;++oy){
        int sy=(int)((int64_t)oy*test_info.height/out_h);
        for(int ox=0;ox<out_w;++ox){
            int sx=(int)((int64_t)ox*test_info.width/out_w);
            size_t o=((size_t)sy*test_info.width+sx)*3;
            lv_canvas_set_px(sstv_canvas,x0+ox,y0+oy,lv_color_make(test_rgb[o],test_rgb[o+1],test_rgb[o+2]));
        }
    }
    lv_obj_invalidate(sstv_canvas);
}

/* ---- RX: capture, VIS, decoding threads, audio callback ---- */
#include "dialog_sstv_rx.inc"

/* ---- TX: transmitter thread and interactive TX wizard ---- */
#include "dialog_sstv_tx.inc"

/* ---- Saved-pictures browser ---- */
#include "dialog_sstv_browser.inc"

static void construct_cb(lv_obj_t *parent)
{
    if (dsp_audio_sub_id == AUDIO_SUB_INVALID)
        dsp_audio_sub_id = dsp_audio_subscribe_float(audio_cb, SSTV_SAMPLE_RATE);

    dsp_audio_set_active(dsp_audio_sub_id, true);

    /* Use the native 1.0.2 dialog frame, exactly like FT8. */
    dialog.obj = dialog_init(parent);

    if (param_f_get(cfg.pwr()) > MAX_PWR) {
        radio_set_pwr(MAX_PWR);
        msg_schedule_text_fmt("Power was limited to %0.0fW", MAX_PWR);
    }

    sstv_canvas = lv_canvas_create(dialog.obj);
    lv_canvas_set_buffer(
        sstv_canvas,
        sstv_buffer,
        SSTV_CANVAS_W,
        SSTV_CANVAS_H,
        LV_IMG_CF_TRUE_COLOR
    );
    lv_obj_set_size(sstv_canvas, SSTV_CANVAS_W, SSTV_CANVAS_H);
    lv_canvas_fill_bg(sstv_canvas, lv_color_hex(0x182028), LV_OPA_COVER);

    /* The frame is provided by dialog_init(), as in FT8. */

    /* White image sheet: maximum 4:3 SSTV area, on the right side of the panel. */
    lv_obj_align(sstv_canvas, LV_ALIGN_TOP_LEFT, SSTV_IMAGE_X, SSTV_IMAGE_Y);
    lv_obj_set_size(sstv_canvas, SSTV_IMAGE_W, SSTV_IMAGE_H);
    lv_canvas_fill_bg(sstv_canvas, lv_color_white(), LV_OPA_COVER);

    /* Route front-panel key/encoder events to SSTV just like WEFAX/NAVTEX.
     * Without a focused object in keyboard_group, key_cb() never receives
     * the volume knob events while the SSTV dialog is open. */
    lv_group_add_obj(keyboard_group, sstv_canvas);
    lv_obj_add_event_cb(sstv_canvas, key_cb, LV_EVENT_KEY, NULL);
    lv_group_focus_obj(sstv_canvas);
    lv_group_set_editing(keyboard_group, true);

    status_label = lv_label_create(dialog.obj);
    lv_label_set_text(status_label, "Stand by.\nPress Start Auto RX to start Autodecoder.\nPress Start Manual RX to start Manual decoder, press Mode button to cycle all available modes for the Manual decoder.");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &sony_28, 0);
    lv_obj_set_width(status_label, 300);
    lv_obj_set_pos(status_label, 10, 105);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_LEFT, 0);

    progress_bar = lv_bar_create(dialog.obj);
    lv_obj_set_size(progress_bar, 250, 16);
    lv_obj_set_pos(progress_bar, 35, 155);
    lv_bar_set_range(progress_bar, 0, 1000);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    set_receiver_ui_stopped();

    /* RESTART defaults ON: a new valid VIS restarts the current Auto RX capture. */
    btn_restart.label = live_restart_enabled ? "Auto RX\nRestart ON" : "Auto RX\nRestart OFF";
    buttons_disabled(&btn_restart, false);
#if SSTV_PAGE3_MENU
    buttons_disabled(&btn_test, false);
#endif

    shown_rows = 0;
    test_ready = false;
    test_rows = 0;
    live_canvas_clear_pending = false;
    live_abort_requested = false;
    live_mode_ui_pending = false;
    ui_timer = lv_timer_create(ui_timer_cb, 30, NULL);
}

static void destruct_cb(void)
{
    app_info_abort();
    sstv_autodetect_stop();
    dsp_audio_set_active(dsp_audio_sub_id, false);

    /* Closing the SSTV dialog is a hard receiver reset.  Stop both RX paths
     * before destroying the LVGL objects so audio_cb() cannot append to a
     * buffer while it is being released. */
    pthread_mutex_lock(&live_mutex);
    live_enabled = false;
    live_capturing = false;
    live_decode_pending = false;
    cont_capturing = false;
    cont_decode_pending = false;
    pthread_mutex_unlock(&live_mutex);

    /* Never allow the dialog to disappear while its TX worker can still own
     * the modem.  Request abort and wait for radio_set_modem(false). */
    if (tx_thread_valid) {
        tx_abort = true;
        pthread_join(tx_thread, NULL);
        tx_thread_valid = false;
    }
    tx_running = false;
    tx_started = false;

    /* Back to the power setting, which construct_cb() may have capped at
     * MAX_PWR (as FT8 does when it closes). */
    radio_set_pwr(param_f_get(cfg.pwr()));

    if (ui_timer) {
        lv_timer_del(ui_timer);
        ui_timer = NULL;
    }

    /* A decoder thread may already be using live_samples/cont_samples and
     * test_rgb.  Let it leave cleanly before freeing/resetting that state. */
    if (test_thread_valid) {
        pthread_join(test_thread, NULL);
        test_thread_valid = false;
    }

    /* A decoder that was already running can re-arm Auto RX when it exits,
     * so force every receiver flag back to idle after the join as well. */
    pthread_mutex_lock(&live_mutex);
    live_enabled = false;
    live_capturing = false;
    live_decode_pending = false;
    live_decoding = false;
    live_canvas_clear_pending = false;
    live_abort_requested = false;
    live_mode_ui_pending = false;
    live_ring_pos = 0;
    live_ring_count = 0;
    live_wait_samples = 0;
    live_wait_log_mark = 0;

    cont_capturing = false;
    cont_decode_pending = false;
    cont_decoding = false;
    pthread_mutex_unlock(&live_mutex);

    free(live_samples);
    live_samples = NULL;
    live_written = 0;
    live_target = 0;

    free(cont_samples);
    cont_samples = NULL;
    cont_written = 0;
    cont_capacity = 0;

    test_running = false;
    test_ready = false;
    test_rows = 0;
    shown_rows = 0;
    saved_overlay_pending = false;
    saved_overlay_name[0] = '\0';

    /* Button_data objects are static and survive dialog destruction.  Restore
     * their default labels/mode now so the next open always starts clean. */
    cont_mode = SSTV_MODE_ROBOT8_BW;
    cont_decode_mode = SSTV_MODE_ROBOT8_BW;
    cont_rx_mode = SSTV_MODE_UNKNOWN;
    cont_event = SSTV_CONT_EV_NONE;
    btn_start.label = "Start\nAuto RX";
    btn_cont_start.label = "Start\nManual RX";
    btn_cont_mode.label = "RX Mode\nAutodetect";
    btn_tx.label = "TX";
    tx_ui_state = SSTV_TX_UI_RX;
    tx_restore_auto_rx = false;
    btn_page_1.items[3] = &btn_cont_mode;

    tx_color_selector_destroy();
    tx_target_selector_destroy();
    tx_template_selector_destroy();
    tx_type_selector_destroy();
    tx_picture_selector_destroy();
    tx_instruction_hide();
    tx_phase = SSTV_TX_PHASE_NONE;
    sstv_browser_close();

    fprintf(stderr, "SSTV: dialog closed; Auto RX and Manual RX reset to defaults\n");

    sstv_canvas = NULL;
    sstv_frame = NULL;
    status_label = NULL;
    progress_bar = NULL;
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* AUTODETECT DSP runs in the audio callback; only UI notification happens
     * here in the LVGL thread.  A completed/timeout session stays stopped until
     * AUTODETECT is explicitly pressed again. */
    sstv_autodetect_state_t ad_state = sstv_autodetect_state();
    if (ad_state != autodetect_last_ui_state) {
        autodetect_last_ui_state = ad_state;
        if (ad_state == SSTV_AUTODETECT_DETECTED) {
            sstv_autodetect_mode_t mode = sstv_autodetect_result();
            msg_schedule_text_fmt("SSTV mode detected: %s", sstv_autodetect_mode_name(mode));
        } else if (ad_state == SSTV_AUTODETECT_TIMEOUT) {
            msg_schedule_text_fmt("Unable to detect SSTV mode");
        }
    }

    if (unsupported_vis_pending) {
        int vis = unsupported_vis_code;
        char name[sizeof(unsupported_vis_name)];
        snprintf(name, sizeof(name), "%s", unsupported_vis_name);
        unsupported_vis_pending = false;
        msg_schedule_text_fmt("Unsupported SSTV mode: %s (VIS %d)", name, vis);
    }

    /* Initial Auto-RX VIS detector.
     *
     * IMPORTANT: live_find_vis() must never run in audio_cb().  Negative VIS
     * scans can be much more expensive than successful legacy scans and were
     * causing the 12-kHz subscriber to fall behind by many seconds.  The audio
     * callback now only maintains the rolling ring and raises this flag.
     *
     * As with the restart detector, copy a one-second snapshot under the
     * mutex, then perform the expensive DSP search without the mutex and
     * outside the real-time audio callback. */
    bool initial_scan = false;
    uint64_t initial_window_end = 0;
    size_t initial_ring_count = 0;
    uint64_t initial_wait_samples = 0;
    uint64_t initial_check_no = 0;

    pthread_mutex_lock(&live_mutex);
    if ((live_enabled || cont_capturing) && !live_capturing &&
        !live_decode_pending && !live_decoding &&
        live_vis_window_ready(&live_scan_next_end, "initial")) {
        live_vis_copy_window(live_scan_next_end);
        initial_window_end = live_scan_next_end;
        live_scan_next_end += SSTV_VIS_CHECK_STEP;
        initial_ring_count=live_ring_count;
        initial_wait_samples=live_wait_samples;
        live_dbg_vis_checks++;
        live_dbg_vis_check_samples=live_wait_samples;
        initial_check_no=live_dbg_vis_checks;
        initial_scan=true;
    }
    pthread_mutex_unlock(&live_mutex);

    if (initial_scan) {
        sstv_info_t detected_info;
        struct timespec t0,t1;
        clock_gettime(CLOCK_MONOTONIC,&t0);
        bool found=live_detect_vis(live_vis_window,SSTV_VIS_WINDOW_SAMPLES,&detected_info);
        clock_gettime(CLOCK_MONOTONIC,&t1);
        double scan_ms=sstv_dbg_elapsed(&t0,&t1)*1000.0;

        /* Keep useful diagnostics for expensive NEGATIVE scans too. */
        if (!found && scan_ms >= 20.0) {
            /* fprintf(stderr,
                    "SSTV DBG: VIS negative scan slow check=%llu scan=%.2fms audio_seen=%.3fs\n",
                    (unsigned long long)initial_check_no,scan_ms,
                    (double)initial_wait_samples/SSTV_SAMPLE_RATE); */
        }

        if (found) {
            pthread_mutex_lock(&live_mutex);

            /* A VIS during Manual RX: the picture being captured is over.
             * Close the Manual capture (it is decoded as usual) and hand the
             * new picture over to Auto RX, which stays on afterwards. */
            if (cont_capturing && !live_enabled && !live_capturing &&
                !live_decode_pending && !live_decoding) {
                cont_capturing = false;
                cont_decode_mode = (cont_rx_mode == SSTV_MODE_UNKNOWN) ? cont_detected : cont_rx_mode;
                cont_decode_pending = true;
                cont_end_reason = SSTV_CONT_EV_END_VIS;
                cont_event = SSTV_CONT_EV_END_VIS;
                live_enabled = true;
                live_abort_requested = false;
                fprintf(stderr,"SSTV CONT: VIS %d (%s) received: Manual capture closed after %.1f s, Auto RX takes over\n",
                        detected_info.vis, detected_info.name, (double)cont_written/SSTV_SAMPLE_RATE);
            }
            /* The receiver may have been stopped or changed state while the
             * snapshot was being analysed.  Commit only if we are still
             * waiting for the first VIS. */
            if (live_enabled && !live_capturing &&
                !live_decode_pending && !live_decoding) {
                live_info=detected_info;

                double listen_wall=live_dbg_listen_ts_valid ?
                    sstv_dbg_elapsed(&live_dbg_listen_ts,&t1) : -1.0;
                fprintf(stderr,
                        "SSTV DBG: VIS FOUND wall_since_listen=%.3fs audio_seen=%llu (%.3fs@SR) "
                        "checks=%llu scan=%.2fms ring=%zu window=%d step=%d [async]\n",
                        listen_wall,
                        (unsigned long long)initial_wait_samples,
                        (double)initial_wait_samples/SSTV_SAMPLE_RATE,
                        (unsigned long long)initial_check_no,
                        scan_ms,initial_ring_count,
                        SSTV_VIS_WINDOW_SAMPLES,SSTV_VIS_CHECK_STEP);

                switch (live_info.mode) {
                    case SSTV_MODE_ROBOT8_BW:
                    case SSTV_MODE_ROBOT36:
                    case SSTV_MODE_ROBOT72:
                    case SSTV_MODE_MARTIN_M1:
                    case SSTV_MODE_MARTIN_M2:
                    case SSTV_MODE_SCOTTIE_S1:
                    case SSTV_MODE_SCOTTIE_S2:
                        cont_mode=live_info.mode;
                        live_mode_ui_pending=true;
                        break;
                    default:
                        break;
                }

                live_target=(long)((live_mode_seconds(live_info.mode)+2.0f)*SSTV_SAMPLE_RATE);
                live_samples=malloc((size_t)live_target*sizeof(float));
                if (!live_samples) {
                    fprintf(stderr,"SSTV LIVE: ERROR allocating %.1f MB capture buffer\n",
                            (double)live_target*sizeof(float)/(1024.0*1024.0));
                    live_enabled=false;
                } else {
                    /* Use the CURRENT two-second ring.  audio_cb() kept it
                     * current while the snapshot was analysed, without doing
                     * any VIS DSP itself. */
                    size_t keep=live_ring_count;
                    if ((long)keep>live_target) keep=(size_t)live_target;
                    size_t oldest=(live_ring_pos + SSTV_PREBUF_SAMPLES - live_ring_count)%SSTV_PREBUF_SAMPLES;
                    for (size_t i=0;i<keep;++i)
                        live_samples[i]=live_ring[(oldest+i)%SSTV_PREBUF_SAMPLES];

                    live_written=(long)keep;
                    live_capturing=true;
                    sstv_dbg_capture_start(live_written);
                    live_preview_start(initial_window_end, live_ring_total - live_ring_count);

                    /* The initial detector is done.  If Restart is enabled,
                     * begin its rolling detector only with audio AFTER this
                     * VIS, so it cannot immediately rediscover the same one. */
                    if (live_restart_enabled) {
                        live_ring_pos=0;
                        live_ring_count=0;
                    }

                    live_canvas_clear_pending=true;
                    fprintf(stderr,
                            "SSTV LIVE: capturing %s directly to RAM; target %.1f s, buffer %.1f MB, prebuffer=%ld samples\n",
                            live_info.name,(double)live_target/SSTV_SAMPLE_RATE,
                            (double)live_target*sizeof(float)/(1024.0*1024.0),
                            live_written);
                }
            }
            pthread_mutex_unlock(&live_mutex);
        }
    }

    /* live_find_vis() above may just have queued an unsupported VIS. */
    if (unsupported_vis_pending) {
        int vis = unsupported_vis_code;
        char name[sizeof(unsupported_vis_name)];
        snprintf(name, sizeof(name), "%s", unsupported_vis_name);
        unsupported_vis_pending = false;
        msg_schedule_text_fmt("Unsupported mode: %s (VIS %d)", name, vis);
    }

    /* Auto-RX restart detector.  The audio callback only fills the rolling
     * ring; the windows to scan are scheduled by live_vis_window_ready().  Take a short snapshot while
     * holding live_mutex, then do the expensive VIS search WITHOUT the mutex
     * and outside the real-time audio callback. */
    bool restart_scan = false;
    uint64_t restart_window_end = 0;
    size_t restart_ring_count = 0;
    pthread_mutex_lock(&live_mutex);
    if (live_restart_enabled && live_capturing &&
        live_vis_window_ready(&live_restart_next_end, "restart")) {
        live_vis_copy_window(live_restart_next_end);
        restart_window_end = live_restart_next_end;
        live_restart_next_end += SSTV_VIS_CHECK_STEP;
        restart_ring_count=live_ring_count;
        restart_scan=true;
    }
    pthread_mutex_unlock(&live_mutex);

    if (restart_scan) {
        sstv_info_t restart_info;
        struct timespec t0,t1;
        clock_gettime(CLOCK_MONOTONIC,&t0);
        bool found=live_detect_vis(live_vis_window,SSTV_VIS_WINDOW_SAMPLES,&restart_info);
        clock_gettime(CLOCK_MONOTONIC,&t1);
        if (found) {
            pthread_mutex_lock(&live_mutex);
            if (live_restart_enabled && live_capturing) {
                fprintf(stderr,
                        "SSTV DBG: VIS RESTART FOUND capture_wall=%.3fs scan=%.2fms ring=%zu [async]\n",
                        sstv_dbg_elapsed(&live_dbg_capture_ts,&t1),
                        sstv_dbg_elapsed(&t0,&t1)*1000.0,
                        restart_ring_count);
                long restart_target=(long)((live_mode_seconds(restart_info.mode)+2.0f)*SSTV_SAMPLE_RATE);
                float *restart_samples=malloc((size_t)restart_target*sizeof(float));
                if (!restart_samples) {
                    fprintf(stderr,"SSTV LIVE: RESTART ignored - ERROR allocating %.1f MB capture buffer\n",
                            (double)restart_target*sizeof(float)/(1024.0*1024.0));
                } else {
                    size_t keep=live_ring_count;
                    if ((long)keep>restart_target) keep=(size_t)restart_target;
                    size_t oldest=(live_ring_pos + SSTV_PREBUF_SAMPLES - live_ring_count)%SSTV_PREBUF_SAMPLES;
                    for (size_t i=0;i<keep;++i)
                        restart_samples[i]=live_ring[(oldest+i)%SSTV_PREBUF_SAMPLES];
                    fprintf(stderr,"SSTV LIVE: RESTART - new VIS=%d -> %s; discarding current %s capture at %.1f s\n",
                            restart_info.vis,restart_info.name,live_info.name,
                            (double)live_written/SSTV_SAMPLE_RATE);
                    free(live_samples);
                    live_samples=restart_samples;
                    live_written=(long)keep;
                    live_target=restart_target;
                    live_info=restart_info;
                    live_preview_start(restart_window_end, live_ring_total - live_ring_count);
                    sstv_dbg_capture_start(live_written);
                    live_canvas_clear_pending=true;
                    switch (live_info.mode) {
                        case SSTV_MODE_ROBOT8_BW:
                        case SSTV_MODE_ROBOT36:
                        case SSTV_MODE_ROBOT72:
                        case SSTV_MODE_MARTIN_M1:
                        case SSTV_MODE_MARTIN_M2:
                        case SSTV_MODE_SCOTTIE_S1:
                        case SSTV_MODE_SCOTTIE_S2:
                            cont_mode=live_info.mode;
                            live_mode_ui_pending=true;
                            break;
                        default: break;
                    }
                    live_ring_pos=0;
                    live_ring_count=0;
                    fprintf(stderr,"SSTV LIVE: RESTART capturing %s from new VIS; target %.1f s, prebuffer=%ld samples\n",
                            live_info.name,(double)live_target/SSTV_SAMPLE_RATE,live_written);
                }
            }
            pthread_mutex_unlock(&live_mutex);
        }
    }

    if (tx_target_keyboard_open_pending) {
        tx_target_keyboard_open_pending=false;
        tx_target_keyboard_open_now();
    }

    if (tx_target_keyboard_force_until != 0) {
        uint32_t now=lv_tick_get();
        if ((int32_t)(tx_target_keyboard_force_until-now) > 0) {
            tx_target_keyboard_force_lower();
        } else {
            tx_target_keyboard_force_lower();
            tx_target_keyboard_force_until=0;
        }
    }

    /* audio_cb() runs outside the LVGL/UI context.  It only requests the
     * clear; the actual canvas operation is performed here by the UI timer. */
    if (live_canvas_clear_pending) {
        live_canvas_clear_pending = false;
        if (sstv_canvas) {
            lv_canvas_fill_bg(sstv_canvas, lv_color_white(), LV_OPA_COVER);
            lv_obj_invalidate(sstv_canvas);
        }
    }
    live_preview_tick();
    cont_preview_tick();

    /* VIS detection runs in the audio callback.  Update LVGL/button text only
     * here, in the UI context. */
    if (live_mode_ui_pending) {
        live_mode_ui_pending = false;
        snprintf(tx_mode_label,sizeof(tx_mode_label),"RX Mode\n%s",cont_rx_mode_name());
        btn_cont_mode.label=tx_mode_label;
        if (btn_cont_mode.disp_btn) buttons_refresh(&btn_cont_mode);
    }

    if (tx_ui_state == SSTV_TX_UI_SETUP &&
        tx_phase == SSTV_TX_PHASE_SELECT_PICTURE)
        tx_picture_load_pending_preview();

    if (tx_ui_state == SSTV_TX_UI_SETUP &&
        tx_phase == SSTV_TX_PHASE_SELECT_TEMPLATE &&
        tx_template_preview_pending) {
        tx_template_preview_pending = false;
        tx_template_render_selected();
    }

    if (tx_ui_state == SSTV_TX_UI_SETUP &&
        tx_phase == SSTV_TX_PHASE_SELECT_COLORS &&
        tx_color_preview_pending &&
        (uint32_t)(lv_tick_get()-tx_color_preview_since_ms) >= 300) {
        tx_color_preview_pending = false;
        tx_color_render_selected();
    }

    if (saved_overlay_pending) {
        char message[160];
        saved_overlay_pending = false;
        snprintf(message, sizeof(message), "SSTV picture %s saved", saved_overlay_name);
        msg_schedule_text_fmt("%s", message);
    }

    if (tx_running) {
        if (progress_bar) {
            lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
            int v = 0;
            if (tx_key_time_valid) {
                uint64_t keyed_ms = monotonic_ms_since(&tx_key_time);
                if (keyed_ms > SSTV_TX_PREROLL_MS) {
                    uint64_t payload_ms = keyed_ms - SSTV_TX_PREROLL_MS;
                    unsigned int duration_ms = sstv_tx_duration_ms(tx_mode);
                    if (duration_ms > 0) {
                        if (payload_ms >= duration_ms) v = 1000;
                        else v = (int)((payload_ms * 1000u) / duration_ms);
                    }
                }
            }
            tx_progress = v;
            lv_bar_set_value(progress_bar,v,LV_ANIM_OFF);
        }
        if (tx_started) { char msg[96]; snprintf(msg,sizeof(msg),"Transmitting %s...",cont_mode_name(tx_mode)); set_status(msg); }
    } else if (tx_thread_valid) {
        pthread_join(tx_thread, NULL);
        tx_thread_valid = false;
        if (progress_bar) {
            lv_bar_set_value(progress_bar, tx_result == 1 ? 1000 : 0, LV_ANIM_OFF);
            lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        if (tx_ui_state == SSTV_TX_UI_RF) {
            if (tx_result == 1) {
                /* Commit LAST TX only after the RF worker has completed the
                 * entire transmission successfully.  Aborts/errors leave the
                 * previous backup untouched. */
                size_t last_bytes=(size_t)tx_picture_width*tx_picture_height*3;
                if (tx_picture_width && tx_picture_height &&
                    last_bytes <= sizeof(tx_last_rgb)) {
                    memcpy(tx_last_rgb,tx_final_rgb,last_bytes);
                    tx_last_width=tx_picture_width;
                    tx_last_height=tx_picture_height;
                    tx_last_mode=tx_mode;
                    tx_last_valid=true;
                    fprintf(stderr,"SSTV TX: saved LAST TX (%ux%u, %s)\n",
                            tx_last_width,tx_last_height,cont_mode_name(tx_last_mode));
                }
                char msg[96];
                snprintf(msg,sizeof(msg),"SSTV %s TX complete",cont_mode_name(tx_mode));
                msg_schedule_text_fmt("%s", msg);
            } else if (tx_result == -1) {
                msg_schedule_text_fmt("SSTV TX halted");
            } else {
                char msg[96];
                snprintf(msg,sizeof(msg),"SSTV TX failed (%d)",tx_result);
                msg_schedule_text_fmt("%s", msg);
            }

            /* QSO workflow: completion, Halt and even an encoder/audio error
             * all leave the radio immediately listening in Auto RX. */
            tx_started=false;
            tx_progress=0;
            tx_return_to_auto_rx();
        } else {
            if (tx_result == 1) {
                char msg[96];
                snprintf(msg,sizeof(msg),"%s test TX complete.",cont_mode_name(tx_mode));
                set_status(msg);
                snprintf(msg,sizeof(msg),"SSTV %s test TX complete",cont_mode_name(tx_mode));
                msg_schedule_text_fmt("%s", msg);
            } else if (tx_result == -1) {
                set_status("SSTV test TX aborted.");
            } else {
                char msg[96];
                snprintf(msg,sizeof(msg),"SSTV test TX failed (%d).",tx_result);
                set_status(msg);
            }
            tx_started=false;
            tx_progress=0;
        }
    }

    if (test_thread_valid && !test_running) {
        pthread_join(test_thread, NULL);
        test_thread_valid = false;
    }

    if (cont_event != SSTV_CONT_EV_NONE) {
        sstv_cont_event_t ev = cont_event;
        cont_event = SSTV_CONT_EV_NONE;
        char msg[128];
        switch (ev) {
        case SSTV_CONT_EV_LOCK:
            if (!cont_pv_on && cont_capturing) {
                sstv_preview_init(&cont_pv, cont_detected, -1.0);
                cont_pv_on = true;
            }
            snprintf(msg, sizeof(msg), "SSTV Manual decoder is running\n\n%s detected\n\nPress Stop Manual RX to render received picture",
                     cont_mode_name(cont_detected));
            set_status(msg);
            msg_schedule_text_fmt("SSTV mode detected: %s", cont_mode_name(cont_detected));
            break;
        case SSTV_CONT_EV_END_SYNC:
        case SSTV_CONT_EV_END_LIMIT:
        case SSTV_CONT_EV_END_VIS:
            btn_cont_start.label = "Start\nManual RX";
            if (btn_cont_start.disp_btn) buttons_refresh(&btn_cont_start);
            if (ev == SSTV_CONT_EV_END_SYNC)
                msg_schedule_text_fmt("End of SSTV picture detected");
            if (ev == SSTV_CONT_EV_END_VIS) {
                msg_schedule_text_fmt("New SSTV picture: Auto RX started");
                set_receiver_ui_listening();
            }
            update_rx_controls();
            break;
        case SSTV_CONT_EV_DETECT_TIMEOUT: {
            /* No lock within SSTV_CONT_DETECT_SECONDS.  Last attempt: the
             * whole-capture classification on what has been captured.  The
             * audio thread only appends beyond cont_written and never frees
             * cont_samples; Stop runs in this same LVGL thread. */
            if (!cont_capturing || cont_locked) break;
            pthread_mutex_lock(&live_mutex);
            long w = cont_written;
            pthread_mutex_unlock(&live_mutex);
            long ev_n = 0;
            sstv_mode_t m = sstv_linesync_classify(cont_samples, w, SSTV_MODE_UNKNOWN, &ev_n);
            pthread_mutex_lock(&live_mutex);
            if (!cont_capturing || cont_locked) {           /* changed meanwhile */
                pthread_mutex_unlock(&live_mutex);
                break;
            }
            if (m != SSTV_MODE_UNKNOWN) {
                /* Continue with this mode: force the tracker on it so the
                 * end of picture is still detected. */
                cont_locked = true;
                cont_detected = m;
                long lim = (long)((sstv_linesync_picture_seconds(m) + 5.0) * SSTV_SAMPLE_RATE);
                if (lim < cont_limit) cont_limit = lim;
                sstv_linesync_init(&cont_ls, m);
                sstv_preview_init(&cont_pv, m, -1.0);
                cont_pv_on = true;
                pthread_mutex_unlock(&live_mutex);
                fprintf(stderr,"SSTV CONT: no lock in %d s, capture classified as %s (%ld intervals): continuing\n",
                        SSTV_CONT_DETECT_SECONDS, cont_mode_name(m), ev_n);
                snprintf(msg, sizeof(msg), "SSTV Manual decoder is running\n\n%s detected\n\nPress Stop Manual RX to render received picture",
                         cont_mode_name(m));
                set_status(msg);
                msg_schedule_text_fmt("SSTV mode detected: %s", cont_mode_name(m));
                break;
            }
            cont_capturing = false;
            free(cont_samples); cont_samples = NULL; cont_capacity = cont_written = 0;
            pthread_mutex_unlock(&live_mutex);
            fprintf(stderr,"SSTV CONT: no SSTV mode detected in %d s; Manual RX stopped\n", SSTV_CONT_DETECT_SECONDS);
            btn_cont_start.label = "Start\nManual RX";
            if (btn_cont_start.disp_btn) buttons_refresh(&btn_cont_start);
            reset_ui_to_standby();
            msg_schedule_text_fmt("No SSTV mode detected in %d s", SSTV_CONT_DETECT_SECONDS);
            break;
        }
        case SSTV_CONT_EV_NO_LOCK:
            msg_schedule_text_fmt("No %s sync found: picture may be wrong", cont_mode_name(cont_rx_mode));
            break;
        case SSTV_CONT_EV_MISMATCH:
            msg_schedule_text_fmt("Signal looks like %s, decoded as %s",
                                  cont_mode_name(cont_class_mode), cont_mode_name(cont_rx_mode));
            break;
        case SSTV_CONT_EV_NO_SIGNAL:
            msg_schedule_text_fmt("No SSTV picture detected");
            break;
        default:
            break;
        }
    }

    if (cont_decode_pending && !test_running && !test_thread_valid) {
        cont_decode_pending = false;
        cont_decoding = true;
        fprintf(stderr, "SSTV CONT: STOP - starting manual RAM decoder\n");
        char status[96];
        snprintf(status, sizeof(status), "Decoding Manual %s",
                 cont_decode_mode == SSTV_MODE_UNKNOWN ? "capture" : cont_mode_name(cont_decode_mode));
        set_status(status);
        if (progress_bar) lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        shown_rows=0; test_rows=0; test_ready=false; test_running=true;
        update_rx_controls();
        int rc=pthread_create(&test_thread,NULL,cont_decode_thread_cb,NULL);
        if (rc==0) test_thread_valid=true;
        else {
            fprintf(stderr,"SSTV CONT: decoder pthread_create failed: %d\n",rc);
            test_running=false; cont_decoding=false;
            pthread_mutex_lock(&live_mutex);
            free(cont_samples); cont_samples=NULL; cont_capacity=cont_written=0;
            pthread_mutex_unlock(&live_mutex);
            msg_schedule_text_fmt("Decoding failed");
            if (live_enabled) set_receiver_ui_listening(); else reset_ui_to_standby();
        }
    }

    if (live_decode_pending && !test_running && !test_thread_valid) {
        live_decode_pending = false;
        live_decoding = true;
        sstv_dbg_capture_report("DECODE");
        fprintf(stderr, "SSTV LIVE: capture complete; starting direct RAM image decoder\n");
        set_receiver_ui_decoding(live_info.name);
        if (progress_bar) lv_bar_set_value(progress_bar,1000,LV_ANIM_OFF);
        shown_rows=0; test_rows=0; test_ready=false; test_running=true;
        int rc=pthread_create(&test_thread,NULL,live_decode_thread_cb,NULL);
        if (rc==0) test_thread_valid=true;
        else { fprintf(stderr,"SSTV LIVE: decoder pthread_create failed: %d\n",rc); test_running=false; live_decoding=false; }
    }

    /* Live progress follows the expected capture duration. */
    if (live_capturing && live_target > 0) {
        set_receiver_ui_decoding(live_info.name);
        if (progress_bar) {
            int v=(int)((live_written*1000L)/live_target); if(v<0)v=0; if(v>1000)v=1000;
            lv_bar_set_value(progress_bar,v,LV_ANIM_OFF);
        }
    }

    /* A decode that produced no rows (Auto, Manual or Test RX) must never
     * leave the receiver stuck: Auto RX goes back to listening, otherwise
     * back to stand-by.  (Before, only the Auto case was handled and a
     * failed Manual decode froze the UI on "Decoding...".) */
    if (test_ready && test_rows <= 0) {
        test_ready = false;
        shown_rows = 0;
        if (progress_bar)
            lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        if (live_enabled) {
            msg_schedule_text_fmt("Decoding failed");
            set_receiver_ui_listening();
        } else {
            if (!cont_no_signal) msg_schedule_text_fmt("Decoding failed");
            reset_ui_to_standby();
        }
        cont_no_signal = false;
        return;
    }

    if (!sstv_canvas || !test_ready || shown_rows >= test_rows) return;
    render_scaled_image();
    shown_rows=test_rows;
    if (progress_bar) lv_bar_set_value(progress_bar,1000,LV_ANIM_OFF);
    if (live_enabled)
        set_receiver_ui_listening();
    else
        reset_ui_to_standby();
}


static void key_cb(lv_event_t *e)
{
    uint32_t key = *((uint32_t *)lv_event_get_param(e));
    switch (key) {
        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            return;
        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            return;
        default:
            break;
    }
}

static void restart_cb(button_data_t *data)
{
    (void)data;

    pthread_mutex_lock(&live_mutex);
    live_restart_enabled = !live_restart_enabled;

    /* When enabling during an active capture, start the parallel VIS detector
     * from fresh audio.  This prevents the VIS which started the current
     * picture from immediately being detected again as a restart. */
    if (live_restart_enabled && live_capturing) {
        live_ring_pos = 0;
        live_ring_count = 0;
    }
    pthread_mutex_unlock(&live_mutex);

    btn_restart.label = live_restart_enabled ? "Auto RX\nRestart ON" : "Auto RX\nRestart OFF";
    if (btn_restart.disp_btn) buttons_refresh(&btn_restart);
    fprintf(stderr,"SSTV LIVE: VIS restart %s\n",live_restart_enabled ? "ON" : "OFF");
}

static void close_cb(button_data_t *data)
{
    (void)data;
    dialog_destruct();
}

#if SSTV_PAGE3_MENU
static void test_cb(button_data_t *data)
{
    (void)data;

    if (!sstv_canvas || test_running || cont_capturing || cont_decode_pending || cont_decoding)
        return;

    if (live_enabled || live_capturing) {
        pthread_mutex_lock(&live_mutex);
        live_enabled=false; live_capturing=false;
        free(live_samples); live_samples=NULL; live_written=live_target=0;
        pthread_mutex_unlock(&live_mutex);
        fprintf(stderr,"SSTV LIVE: paused by WAV Test\n");
    }

    if (test_thread_valid) {
        pthread_join(test_thread, NULL);
        test_thread_valid = false;
    }

    lv_canvas_fill_bg(sstv_canvas, lv_color_white(), LV_OPA_COVER);
    set_status("Decoding Test WAV");
    if (progress_bar) {
        lv_bar_set_value(progress_bar,0,LV_ANIM_OFF);
        lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    shown_rows = 0;
    test_rows = 0;
    test_ready = false;
    test_running = true;

    int rc = pthread_create(&test_thread, NULL, test_thread_cb, NULL);
    if (rc != 0) {
        fprintf(stderr, "SSTV TEST: pthread_create failed: %d\n", rc);
        test_running = false;
        return;
    }
    test_thread_valid = true;
}
#endif

static void page_1_cb(button_data_t *data)
{
    (void)data;
    if (sstv_browser_open)
        sstv_browser_close();
    buttons_load_page(&btn_page_1);
}

static void page_2_cb(button_data_t *data)
{
    (void)data;
    buttons_load_page(&btn_page_2);
    sstv_browser_update_buttons();
}

static void page_3_cb(button_data_t *data)
{
    (void)data;
    if (sstv_browser_open)
        sstv_browser_close();
    buttons_load_page(&btn_page_3);
}

#if SSTV_PAGE3_MENU

#if SSTV_PAGE3_AUTODETECT
static void autodetect_cb(button_data_t *data)
{
    (void)data;

    sstv_autodetect_start();
    autodetect_last_ui_state = SSTV_AUTODETECT_LISTENING;
    msg_schedule_text_fmt("SSTV AUTODETECT: listening...");
}
#endif /* SSTV_PAGE3_AUTODETECT */
#endif /* SSTV_PAGE3_MENU */

static void browser_cb(button_data_t *data)
{
    (void)data;

    if (sstv_browser_open)
        sstv_browser_close();
    else
        sstv_browser_build();
}

static void delete_picture_cb(button_data_t *data)
{
    (void)data;

    if (sstv_browser_preview)
        sstv_browser_return_to_list();
    else
        sstv_browser_delete_selected();
}

static void cont_mode_cb(button_data_t *data)
{
    (void)data;
    if (tx_ui_state == SSTV_TX_UI_RF) return;
    if (tx_ui_state == SSTV_TX_UI_RX &&
        (live_enabled || live_capturing || cont_capturing || cont_decode_pending || test_running))
        return;
    if (tx_ui_state == SSTV_TX_UI_RX) {
        unsigned count = sizeof(cont_rx_modes)/sizeof(cont_rx_modes[0]);
        unsigned idx = 0;
        while (idx < count && cont_rx_modes[idx] != cont_rx_mode) ++idx;
        cont_rx_mode = cont_rx_modes[(idx + 1) % count];
        snprintf(tx_mode_label, sizeof(tx_mode_label), "RX Mode\n%s", cont_rx_mode_name());
    } else {
        unsigned count = sizeof(cont_modes)/sizeof(cont_modes[0]);
        unsigned idx = 0;
        while (idx < count && cont_modes[idx] != cont_mode) ++idx;
        cont_mode = cont_modes[(idx + 1) % count];
        snprintf(tx_mode_label, sizeof(tx_mode_label), "TX Mode\n%s", cont_mode_name(cont_mode));
    }
    btn_cont_mode.label = tx_mode_label;
    if (btn_cont_mode.disp_btn) buttons_refresh(&btn_cont_mode);

    if (tx_ui_state == SSTV_TX_UI_SETUP &&
        tx_picture_selected >= 0 &&
        (size_t)tx_picture_selected < tx_picture_file_count) {
        if (tx_picture_load_and_prepare(tx_picture_files[tx_picture_selected]))
            tx_rebuild_current_overlay();
    }
}

static void cont_start_cb(button_data_t *data)
{
    (void)data;
    pthread_mutex_lock(&live_mutex);

    if (cont_capturing) {
        cont_capturing = false;
        /* Autodetect without a recognised mode: there is nothing to decode.
         * Stop really stops: discard the capture and go back to stand-by. */
        bool no_mode = (cont_rx_mode == SSTV_MODE_UNKNOWN && !cont_locked);
        bool had_audio = cont_written > 0;
        if (had_audio && !no_mode) {
            cont_decode_mode = (cont_rx_mode == SSTV_MODE_UNKNOWN) ? cont_detected : cont_rx_mode;
            cont_decode_pending = true;
            fprintf(stderr, "SSTV CONT: STOP requested after %.1f s (%ld samples); decode pending as %s\n",
                    (double)cont_written/SSTV_SAMPLE_RATE, cont_written, cont_mode_name(cont_decode_mode));
        } else {
            free(cont_samples); cont_samples=NULL; cont_capacity=cont_written=0;
            cont_event = SSTV_CONT_EV_NONE;
            fprintf(stderr, "SSTV CONT: STOP requested, %s; capture discarded\n",
                    had_audio ? "no SSTV mode recognised yet" : "no audio captured");
        }
        pthread_mutex_unlock(&live_mutex);
        btn_cont_start.label = "Start\nManual RX";
        if (btn_cont_start.disp_btn) buttons_refresh(&btn_cont_start);
        if (!had_audio || no_mode) {
            reset_ui_to_standby();
            if (had_audio) msg_schedule_text_fmt("Manual RX stopped: no SSTV mode detected");
        }
        update_rx_controls();
        return;
    }

    if (live_enabled || live_capturing || live_decode_pending || test_running) {
        pthread_mutex_unlock(&live_mutex);
        return;
    }

    /* Forced mode: limit = full picture duration from Start (+5 s).
     * Autodetect: allocate for the longest mode; the limit is set when the
     * tracker recognises the mode (audio_cb). */
    float cont_limit_seconds = (cont_rx_mode == SSTV_MODE_UNKNOWN)
        ? (float)SSTV_CONT_MAX_SECONDS : cont_mode_seconds(cont_rx_mode) + 5.0f;
    if (cont_limit_seconds > (float)SSTV_CONT_MAX_SECONDS)
        cont_limit_seconds = (float)SSTV_CONT_MAX_SECONDS;
    cont_capacity = (long)(cont_limit_seconds * (float)SSTV_SAMPLE_RATE);
    cont_limit = cont_capacity;
    sstv_linesync_init(&cont_ls, cont_rx_mode);
    /* forced mode: the preview waits for the tracker lock; Autodetect: it
     * starts when the mode is recognised (SSTV_CONT_EV_LOCK) */
    cont_pv_on = (cont_rx_mode != SSTV_MODE_UNKNOWN);
    if (cont_pv_on) sstv_preview_init(&cont_pv, cont_rx_mode, -1.0);
    cont_locked = false;
    cont_timeout_checked = false;
    cont_detected = SSTV_MODE_UNKNOWN;
    cont_end_reason = SSTV_CONT_EV_NONE;
    cont_event = SSTV_CONT_EV_NONE;
    cont_samples = malloc((size_t)cont_capacity * sizeof(float));
    if (!cont_samples) {
        cont_capacity=0;
        pthread_mutex_unlock(&live_mutex);
        fprintf(stderr, "SSTV CONT: ERROR allocating capture buffer\n");
        return;
    }
    cont_written=0;
    cont_capturing=true;
    pthread_mutex_unlock(&live_mutex);

    if (sstv_canvas) lv_canvas_fill_bg(sstv_canvas, lv_color_white(), LV_OPA_COVER);
    char status[192];
    if (cont_rx_mode == SSTV_MODE_UNKNOWN)
        snprintf(status, sizeof(status),
                 "SSTV Manual decoder is running\n\nDetecting SSTV mode (up to %d s)...\n\nPress Stop Manual RX to stop",
                 SSTV_CONT_DETECT_SECONDS);
    else
        snprintf(status, sizeof(status),
                 "SSTV Manual decoder is running\n\nDecoding %s signal\n\nPress Stop Manual RX to render received picture",
                 cont_mode_name(cont_rx_mode));
    set_status(status);
    if (progress_bar) lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    btn_cont_start.label = "Stop\nManual RX";
    if (btn_cont_start.disp_btn) buttons_refresh(&btn_cont_start);
    update_rx_controls();
    fprintf(stderr,"SSTV CONT: START - manual %s capture at %d Hz, limit %.1f s\n",
            cont_rx_mode_name(),SSTV_SAMPLE_RATE,cont_limit_seconds);
}

static void start_cb(button_data_t *data)
{
    (void)data;
    pthread_mutex_lock(&live_mutex);

    if (cont_capturing || cont_decode_pending || cont_decoding) {
        pthread_mutex_unlock(&live_mutex);
        return;
    }

    /* While Auto RX is receiving/decoding a detected picture, this button is
     * "Halt Decoding": discard only the current picture and keep Auto RX on. */
    if (live_capturing || live_decode_pending || live_decoding) {
        live_abort_requested=true;
        live_capturing=false;
        live_decode_pending=false;
        live_ring_pos=live_ring_count=0;
        live_wait_samples=live_wait_log_mark=0;
        if (!live_decoding) {
            free(live_samples);
            live_samples=NULL;
            live_written=live_target=0;
            live_abort_requested=false;
        }
        live_enabled=true;
        pthread_mutex_unlock(&live_mutex);
        set_receiver_ui_listening();
        fprintf(stderr,"SSTV LIVE: HALT - current picture discarded; awaiting next VIS\n");
        return;
    }

    if (live_enabled || live_capturing) {
        live_enabled=false; live_capturing=false; live_decode_pending=false;
        live_ring_pos=live_ring_count=0;
        live_wait_samples=live_wait_log_mark=0;
        free(live_samples); live_samples=NULL; live_written=live_target=0;
        pthread_mutex_unlock(&live_mutex);
        reset_ui_to_standby();
        fprintf(stderr,"SSTV LIVE: STOP - receiver returned to stand-by\n");
        return;
    }

    if (!test_running) {
        live_enabled=true; live_capturing=false; live_decode_pending=false;
        live_abort_requested=false;
        live_ring_pos=live_ring_count=0;
        live_wait_samples=live_wait_log_mark=0;
        live_dbg_vis_checks=0;
        live_dbg_vis_check_samples=0;
        clock_gettime(CLOCK_MONOTONIC, &live_dbg_listen_ts);
        live_dbg_listen_ts_valid=true;
        free(live_samples); live_samples=NULL; live_written=live_target=0;
        fprintf(stderr,"SSTV LIVE: START - listening to RX audio at %d Hz\n", SSTV_SAMPLE_RATE);
        fprintf(stderr,"SSTV LIVE: waiting for 1900/1200/1900 VIS leader...\n");
    }
    pthread_mutex_unlock(&live_mutex);
    set_receiver_ui_listening();
}
