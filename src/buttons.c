/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "buttons.h"

#include "styles.h"
#include "main_screen.h"
#include "mfk.h"
#include "vol.h"
#include "msg.h"
#include "rtty.h"
#include "pannel.h"
#include "params/params.h"
#include "dialog.h"
#include "dialog_settings.h"
#include "dialog_swrscan.h"
#include "dialog_ft8.h"
#include "dialog_freq.h"
#include "dialog_gps.h"
#include "dialog_msg_cw.h"
#include "dialog_msg_voice.h"
#include "dialog_recorder.h"
#include "voice.h"
#include "pubsub_ids.h"

#include <stdio.h>

#define BUTTONS     5

typedef struct {
    lv_obj_t        *obj;
    button_item_t   *item;
} button_t;

static uint8_t      btn_height = 62;
static button_t     btn[BUTTONS];
static lv_obj_t     *parent_obj = NULL;

static void button_next_page_cb(lv_event_t * e);
static void button_app_page_cb(lv_event_t * e);
static void button_vol_update_cb(lv_event_t * e);
static void button_mfk_update_cb(lv_event_t * e);
static void button_mem_load_cb(lv_event_t * e);

static void param_changed_cb(void * s, lv_msg_t * m);

static void button_prev_page_cb(void * ptr);
static void button_vol_hold_cb(void * ptr);
static void button_mfk_hold_cb(void * ptr);
static void button_mem_save_cb(void * ptr);

// Label getters

static char * vol_label_getter();
static char * sql_label_getter();
static char * rfg_label_getter();
static char * tx_power_label_getter();

static char * filter_low_label_getter();
static char * filter_high_label_getter();
static char * filter_bw_label_getter();
static char * speaker_mode_label_getter();

static char * mic_sel_label_getter();
static char * h_mic_gain_label_getter();
static char * i_mic_gain_label_getter();
static char * moni_level_label_getter();

static char * charger_label_getter();
static char * rit_label_getter();
static char * xit_label_getter();

static char * agc_hang_label_getter();
static char * agc_knee_label_getter();
static char * agc_slope_label_getter();

static char * key_speed_label_getter();
static char * key_volume_label_getter();
static char * key_train_label_getter();
static char * key_tone_label_getter();

static char * key_mode_label_getter();
static char * iambic_mode_label_getter();
static char * qsk_time_label_getter();
static char * key_ratio_label_getter();

static char * cw_decoder_label_getter();
static char * cw_tuner_label_getter();
static char * cw_snr_label_getter();

static char * cw_peak_beta_label_getter();
static char * cw_noise_beta_label_getter();

static char * dnf_label_getter();
static char * dnf_center_label_getter();
static char * dnf_width_label_getter();

static char * nb_label_getter();
static char * nb_level_label_getter();
static char * nb_width_label_getter();

static char * nr_label_getter();
static char * nr_level_label_getter();

static void button_action_cb(lv_event_t * e);

static button_page_t    buttons_page = PAGE_VOL_1;

static button_item_t    buttons[] = {
    { .label_type = LABEL_TEXT, .label = "(VOL 1:4)",               .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_2, .prev = PAGE_MEM_2, .voice = "Volume|page 1" },
    { .label_type = LABEL_FN,   .label_fn = vol_label_getter,       .press = button_vol_update_cb,                                  .data = VOL_VOL },
    { .label_type = LABEL_FN,   .label_fn = sql_label_getter,       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_SQL },
    { .label_type = LABEL_FN,   .label_fn = rfg_label_getter,       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_RFG },
    { .label_type = LABEL_FN,   .label_fn = tx_power_label_getter,  .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_PWR },

    { .label_type = LABEL_TEXT, .label = "(VOL 2:4)",                   .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_3, .prev = PAGE_VOL_1, .voice = "Volume|page 2" },
    { .label_type = LABEL_FN,   .label_fn = filter_low_label_getter,    .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_FILTER_LOW },
    { .label_type = LABEL_FN,   .label_fn = filter_high_label_getter,   .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_FILTER_HIGH },
    { .label_type = LABEL_FN,   .label_fn = filter_bw_label_getter,     .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_FILTER_BW },
    { .label_type = LABEL_FN,   .label_fn = speaker_mode_label_getter,  .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_SPMODE },

    { .label_type = LABEL_TEXT, .label = "(VOL 3:4)",                   .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_4, .prev = PAGE_VOL_2, .voice = "Volume|page 3" },
    { .label_type = LABEL_FN,   .label_fn = mic_sel_label_getter,       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_MIC },
    { .label_type = LABEL_FN,   .label_fn = h_mic_gain_label_getter,    .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_HMIC },
    { .label_type = LABEL_FN,   .label_fn = i_mic_gain_label_getter,    .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_IMIC },
    { .label_type = LABEL_FN,   .label_fn = moni_level_label_getter,    .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_MONI },

    { .label_type = LABEL_TEXT, .label = "(VOL 4:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_1, .prev = PAGE_VOL_3, .voice = "Volume|page 4" },
    { .label_type = LABEL_TEXT, .label = "Voice",             .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_LANG },
    { .label_type = LABEL_TEXT, .label = "Voice\nRate",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_RATE },
    { .label_type = LABEL_TEXT, .label = "Voice\nPitch",      .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_PITCH },
    { .label_type = LABEL_TEXT, .label = "Voice\nVolume",     .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_VOLUME },

    { .label_type = LABEL_TEXT, .label = "(MFK 1:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_2, .prev = PAGE_VOL_4, .voice = "MFK|page 1" },
    { .label_type = LABEL_TEXT, .label = "Min\nLevel",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_MIN_LEVEL },
    { .label_type = LABEL_TEXT, .label = "Max\nLevel",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_MAX_LEVEL },
    { .label_type = LABEL_TEXT, .label = "Spectrum\nZoom",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_FACTOR },
    { .label_type = LABEL_TEXT, .label = "Spectrum\nBeta",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_BETA },

    { .label_type = LABEL_TEXT, .label = "(MFK 2:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_3, .prev = PAGE_MFK_1, .voice = "MFK|page 2" },
    { .label_type = LABEL_TEXT, .label = "Spectrum\nFill",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_FILL },
    { .label_type = LABEL_TEXT, .label = "Spectrum\nPeak",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_PEAK },
    { .label_type = LABEL_TEXT, .label = "Peaks\nHold",       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_PEAK_HOLD },
    { .label_type = LABEL_TEXT, .label = "Peaks\nSpeed",      .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_PEAK_SPEED },

    { .label_type = LABEL_TEXT, .label = "(MFK 3:4)",               .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_4, .prev = PAGE_MFK_2, .voice = "MFK|page 3" },
    { .label_type = LABEL_FN,   .label_fn = charger_label_getter,   .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CHARGER },
    { .label_type = LABEL_TEXT, .label = "Antenna",                 .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_ANT },
    { .label_type = LABEL_FN,   .label_fn = rit_label_getter,       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_RIT },
    { .label_type = LABEL_FN,   .label_fn = xit_label_getter,       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_XIT },

    { .label_type = LABEL_TEXT, .label = "(MFK 4:4)",               .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MEM_1, .prev = PAGE_MFK_3, .voice = "MFK|page 4" },
    { .label_type = LABEL_FN,   .label_fn = agc_hang_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_HANG },
    { .label_type = LABEL_FN,   .label_fn = agc_knee_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_KNEE },
    { .label_type = LABEL_FN,   .label_fn = agc_slope_label_getter, .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_SLOPE },
    { .label_type = LABEL_TEXT, .label = "",                        .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(MEM 1:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MEM_2, .prev = PAGE_MFK_4, .voice = "Memory|page 1" },
    { .label_type = LABEL_TEXT, .label = "Set 1",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 1 },
    { .label_type = LABEL_TEXT, .label = "Set 2",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 2 },
    { .label_type = LABEL_TEXT, .label = "Set 3",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 3 },
    { .label_type = LABEL_TEXT, .label = "Set 4",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 4 },

    { .label_type = LABEL_TEXT, .label = "(MEM 2:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_1, .prev = PAGE_MEM_1, .voice = "Memory|page 2" },
    { .label_type = LABEL_TEXT, .label = "Set 5",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 5 },
    { .label_type = LABEL_TEXT, .label = "Set 6",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 6 },
    { .label_type = LABEL_TEXT, .label = "Set 7",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 7 },
    { .label_type = LABEL_TEXT, .label = "Set 8",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 8 },

    /* CW */

    { .label_type = LABEL_TEXT, .label = "(KEY 1:2)",                .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_KEY_2, .prev = PAGE_CW_DECODER_2, .voice = "Key|page 1" },
    { .label_type = LABEL_FN,   .label_fn = key_speed_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_SPEED },
    { .label_type = LABEL_FN,   .label_fn = key_volume_label_getter, .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_VOL },
    { .label_type = LABEL_FN,   .label_fn = key_train_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_TRAIN },
    { .label_type = LABEL_FN,   .label_fn = key_tone_label_getter,   .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_TONE },

    { .label_type = LABEL_TEXT, .label = "(KEY 2:2)",                 .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_CW_DECODER_1, .prev = PAGE_KEY_1, .voice = "Key|page 2" },
    { .label_type = LABEL_FN,   .label_fn = key_mode_label_getter,    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_MODE },
    { .label_type = LABEL_FN,   .label_fn = iambic_mode_label_getter, .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_IAMBIC_MODE },
    { .label_type = LABEL_FN,   .label_fn = qsk_time_label_getter,    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_QSK_TIME },
    { .label_type = LABEL_FN,   .label_fn = key_ratio_label_getter,   .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_RATIO },

    { .label_type = LABEL_TEXT, .label = "(CW 1:2)",                    .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_CW_DECODER_2, .prev = PAGE_KEY_2, .voice = "CW|page 1" },
    { .label_type = LABEL_FN,   .label_fn = cw_decoder_label_getter,    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER },
    { .label_type = LABEL_FN,   .label_fn = cw_tuner_label_getter,      .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_TUNE },
    { .label_type = LABEL_FN,   .label_fn = cw_snr_label_getter,        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_SNR },
    { .label_type = LABEL_TEXT, .label = "",                            .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(CW 2:2)",                    .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_KEY_1, .prev = PAGE_CW_DECODER_1, .voice = "CW|page 2" },
    { .label_type = LABEL_FN,   .label_fn = cw_peak_beta_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_PEAK_BETA },
    { .label_type = LABEL_FN,   .label_fn = cw_noise_beta_label_getter, .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_NOISE_BETA },
    { .label_type = LABEL_TEXT, .label = "",                            .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                            .press = NULL },

    /* DSP */

    { .label_type = LABEL_TEXT, .label = "(DFN 1:3)",                   .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_2, .prev = PAGE_DFN_3, .voice = "DNF page" },
    { .label_type = LABEL_FN,   .label_fn = dnf_label_getter,           .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF },
    { .label_type = LABEL_FN,   .label_fn = dnf_center_label_getter,    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF_CENTER },
    { .label_type = LABEL_FN,   .label_fn = dnf_width_label_getter,     .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF_WIDTH },
    { .label_type = LABEL_TEXT, .label = "",                            .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(DFN 2:3)",               .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_3, .prev = PAGE_DFN_1, .voice = "NB page" },
    { .label_type = LABEL_FN,   .label_fn = nb_label_getter,        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB },
    { .label_type = LABEL_FN,   .label_fn = nb_level_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB_LEVEL },
    { .label_type = LABEL_FN,   .label_fn = nb_width_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB_WIDTH },
    { .label_type = LABEL_TEXT, .label = "",                        .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(DFN 3:3)",               .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_1, .prev = PAGE_DFN_2, .voice = "NR page" },
    { .label_type = LABEL_FN,   .label_fn = nr_label_getter,        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NR },
    { .label_type = LABEL_FN,   .label_fn = nr_level_label_getter,  .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NR_LEVEL },
    { .label_type = LABEL_TEXT, .label = "",                        .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                        .press = NULL },

    /* APP */

    { .label_type = LABEL_TEXT, .label = "(APP 1:3)",         .press = button_next_page_cb,   .next = PAGE_APP_2, .prev = PAGE_APP_3, .voice = "Application|page 1" },
    { .label_type = LABEL_TEXT, .label = "RTTY",              .press = button_app_page_cb,    .data = PAGE_RTTY },
    { .label_type = LABEL_TEXT, .label = "FT8",               .press = button_app_page_cb,    .data = PAGE_FT8 },
    { .label_type = LABEL_TEXT, .label = "SWR\nScan",         .press = button_app_page_cb,    .data = PAGE_SWRSCAN },
    { .label_type = LABEL_TEXT, .label = "GPS",               .press = button_app_page_cb,    .data = PAGE_GPS },

    { .label_type = LABEL_TEXT, .label = "(APP 2:3)",         .press = button_next_page_cb,   .next = PAGE_APP_3, .prev = PAGE_APP_1, .voice = "Application|page 2" },
    { .label_type = LABEL_TEXT, .label = "Recorder",          .press = button_app_page_cb,    .data = PAGE_RECORDER },
    { .label_type = LABEL_TEXT, .label = "QTH",               .press = button_action_cb,      .data = ACTION_APP_QTH },
    { .label_type = LABEL_TEXT, .label = "Callsign",          .press = button_action_cb,      .data = ACTION_APP_CALLSIGN },
    { .label_type = LABEL_TEXT, .label = "Settings",          .press = button_app_page_cb,    .data = PAGE_SETTINGS },

    { .label_type = LABEL_TEXT, .label = "(APP 3:3)",         .press = button_next_page_cb,   .next = PAGE_APP_1, .prev = PAGE_APP_2, .voice = "Application|page 2" },
    { .label_type = LABEL_TEXT, .label = "WiFi",              .press = button_app_page_cb,    .data = PAGE_WIFI },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* RTTY */

    { .label_type = LABEL_TEXT, .label = "(RTTY 1:1)",        .press = NULL },
    { .label_type = LABEL_TEXT, .label = "Rate",              .press = button_mfk_update_cb,  .data = MFK_RTTY_RATE },
    { .label_type = LABEL_TEXT, .label = "Shift",             .press = button_mfk_update_cb,  .data = MFK_RTTY_SHIFT },
    { .label_type = LABEL_TEXT, .label = "Center",            .press = button_mfk_update_cb,  .data = MFK_RTTY_CENTER },
    { .label_type = LABEL_TEXT, .label = "Reverse",           .press = button_mfk_update_cb,  .data = MFK_RTTY_REVERSE },

    /* Settings */

    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* SWR Scan */

    { .label_type = LABEL_TEXT, .label = "Run",               .press = dialog_swrscan_run_cb },
    { .label_type = LABEL_TEXT, .label = "Scale",             .press = dialog_swrscan_scale_cb },
    { .label_type = LABEL_TEXT, .label = "Span",              .press = dialog_swrscan_span_cb },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* FT8 */

    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* GPS */

    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* Msg CW */

    { .label_type = LABEL_TEXT, .label = "(MSG 1:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_CW_2, .prev = PAGE_MSG_CW_2 },
    { .label_type = LABEL_TEXT, .label = "Send",              .press = dialog_msg_cw_send_cb },
    { .label_type = LABEL_TEXT, .label = "Beacon",            .press = dialog_msg_cw_beacon_cb },
    { .label_type = LABEL_TEXT, .label = "Beacon\nPeriod",    .press = dialog_msg_cw_period_cb },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(MSG 2:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_CW_1, .prev = PAGE_MSG_CW_1 },
    { .label_type = LABEL_TEXT, .label = "New",               .press = dialog_msg_cw_new_cb },
    { .label_type = LABEL_TEXT, .label = "Edit",              .press = dialog_msg_cw_edit_cb },
    { .label_type = LABEL_TEXT, .label = "Delete",            .press = dialog_msg_cw_delete_cb },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    /* Msg Voice */

    { .label_type = LABEL_TEXT, .label = "(MSG 1:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_VOICE_2, .prev = PAGE_MSG_VOICE_2 },
    { .label_type = LABEL_TEXT, .label = "Send",              .press = dialog_msg_voice_send_cb },
    { .label_type = LABEL_TEXT, .label = "Beacon",            .press = dialog_msg_voice_beacon_cb },
    { .label_type = LABEL_TEXT, .label = "Beacon\nPeriod",    .press = dialog_msg_voice_period_cb },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },

    { .label_type = LABEL_TEXT, .label = "(MSG 2:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_VOICE_1, .prev = PAGE_MSG_VOICE_1 },
    { .label_type = LABEL_TEXT, .label = "Rec",               .press = dialog_msg_voice_rec_cb },
    { .label_type = LABEL_TEXT, .label = "Rename",            .press = dialog_msg_voice_rename_cb },
    { .label_type = LABEL_TEXT, .label = "Delete",            .press = dialog_msg_voice_delete_cb },
    { .label_type = LABEL_TEXT, .label = "Play",              .press = dialog_msg_voice_play_cb },

    /* Recorder */

    { .label_type = LABEL_TEXT, .label = "(REC 1:1)",         .press = NULL },
    { .label_type = LABEL_TEXT, .label = "Rec",               .press = dialog_recorder_rec_cb },
    { .label_type = LABEL_TEXT, .label = "Rename",            .press = dialog_recorder_rename_cb },
    { .label_type = LABEL_TEXT, .label = "Delete",            .press = dialog_recorder_delete_cb },
    { .label_type = LABEL_TEXT, .label = "Play",              .press = dialog_recorder_play_cb },

    /* WIFI */
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
    { .label_type = LABEL_TEXT, .label = "",                  .press = NULL },
};

void buttons_init(lv_obj_t *parent) {
    uint16_t y = 480 - btn_height;
    uint16_t x = 0;
    uint16_t width = 800 / 5;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_btn_create(parent);

        lv_obj_remove_style_all(f);
        lv_obj_add_style(f, &btn_style, 0);

        lv_obj_set_pos(f, x, y);
        lv_obj_set_size(f, width, btn_height);

        x += width;

        lv_obj_t *label = lv_label_create(f);

        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        btn[i].obj = f;
    }

    parent_obj = parent;
    lv_msg_subscribe(MSG_PARAM_CHANGED, param_changed_cb, NULL);
}

lv_obj_t * buttons_load(uint8_t n, button_item_t *item) {
    lv_obj_t        *label = lv_obj_get_user_data(btn[n].obj);

    lv_obj_remove_event_cb(btn[n].obj, NULL);
    lv_obj_add_event_cb(btn[n].obj, item->press, LV_EVENT_PRESSED, item);
    if (item->label_type == LABEL_TEXT) {
        lv_label_set_text(label, item->label);
    } else if (item->label_type == LABEL_FN) {
        lv_label_set_text(label, item->label_fn());
        lv_obj_set_user_data(label, item->label_fn);
    }

    btn[n].item = item;
    return btn[n].obj;
}

void buttons_load_page(button_page_t page) {
    buttons_page = page;

    for (uint8_t i = 0; i < BUTTONS; i++) {
        buttons_load(i, &buttons[buttons_page * BUTTONS + i]);
    }
}

void buttons_unload_page() {
    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t        *label = lv_obj_get_user_data(btn[i].obj);

        lv_obj_remove_event_cb(btn[i].obj, NULL);
        lv_label_set_text(label, "");
        lv_obj_set_user_data(label, NULL);
        btn[i].item = NULL;
    }
}

static void button_next_page_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    buttons_unload_page();
    buttons_load_page(item->next);

    char *voice = buttons[item->next * BUTTONS].voice;

    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

static void button_app_page_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    main_screen_app(item->data);
}

static void button_action_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    main_screen_action(item->data);
}

static void button_vol_update_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    vol_set_mode(item->data);
    vol_update(0, true);
}

static void button_mfk_update_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    mfk_set_mode(item->data);
    mfk_update(0, true);
}

static void button_prev_page_cb(void * ptr) {
    button_item_t *item = (button_item_t*) ptr;

    buttons_unload_page();
    buttons_load_page(item->prev);

    char *voice = buttons[item->prev * BUTTONS].voice;

    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

static void button_vol_hold_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;
    uint64_t        mask = (uint64_t) 1L << item->data;

    if (params.vol_modes ^ mask) {
        params_lock();
        params.vol_modes ^= mask;
        params_unlock(&params.dirty.vol_modes);

        if (params.vol_modes & mask) {
            msg_update_text_fmt("Added to VOL encoder");
            voice_say_text_fmt("Added to volume encoder");
        } else {
            msg_update_text_fmt("Removed from VOL encoder");
            voice_say_text_fmt("Removed from volume encoder");
        }
    }
}

static void button_mfk_hold_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;
    uint64_t        mask = (uint64_t) 1L << item->data;

    if (params.mfk_modes ^ mask) {
        params_lock();
        params.mfk_modes ^= mask;
        params_unlock(&params.dirty.mfk_modes);

        if (params.mfk_modes & mask) {
            msg_update_text_fmt("Added to MFK encoder");
            voice_say_text_fmt("Added to MFK encoder");
        } else {
            msg_update_text_fmt("Removed from MFK encoder");
            voice_say_text_fmt("Removed from MFK encoder");
        }
    }
}

static void button_mem_load_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    mem_load(item->data);
    voice_say_text_fmt("Memory %i loaded", item->data);
}

static void button_mem_save_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;

    mem_save(item->data);
    voice_say_text_fmt("Memory %i stored", item->data);
}

void buttons_press(uint8_t n, bool hold) {
    if (hold) {
        button_item_t *item = btn[n].item;

        if (item != NULL && item->hold) {
            item->hold(item);
        }
    } else {
        lv_event_send(btn[n].obj, LV_EVENT_PRESSED, NULL);
        lv_event_send(btn[n].obj, LV_EVENT_RELEASED, NULL);
    }
}

static bool get_next_gen_page(button_page_t *next_page) {

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        if ((buttons_page == buttons[i].prev) && (buttons[i].press == button_next_page_cb)) {
            *next_page = i / BUTTONS;
            return true;
        }
    }
    return false;

}

void buttons_load_page_group(button_group_t group) {
    button_page_t next_page;
    bool found_next_page = get_next_gen_page(&next_page);
    switch (group) {
        case GROUP_GEN:
            switch (buttons_page) {
                case PAGE_VOL_1:
                case PAGE_VOL_2:
                case PAGE_VOL_3:
                case PAGE_VOL_4:
                case PAGE_MFK_1:
                case PAGE_MFK_2:
                case PAGE_MFK_3:
                case PAGE_MFK_4:
                case PAGE_MEM_1:
                case PAGE_MEM_2:
                    if (!found_next_page) {
                        next_page = PAGE_VOL_1;
                    }
                    break;
                default:
                    next_page = PAGE_VOL_1;
                    break;
            }
            break;
        case GROUP_APP:
            switch (buttons_page) {
                case PAGE_APP_1:
                case PAGE_APP_2:
                    if (!found_next_page) {
                        next_page = PAGE_APP_1;
                    }
                    break;
                default:
                    next_page = PAGE_APP_1;
                    break;
            }
            break;
        case GROUP_KEY:
            switch (buttons_page) {
                case PAGE_KEY_1:
                case PAGE_KEY_2:
                case PAGE_CW_DECODER_1:
                case PAGE_CW_DECODER_2:
                    if (!found_next_page) {
                        next_page = PAGE_KEY_1;
                    }
                    break;
                default:
                    next_page = PAGE_KEY_1;
                    break;
            }
            break;
        case GROUP_MSG_CW:
            switch (buttons_page) {
                case PAGE_MSG_CW_1:
                case PAGE_MSG_CW_2:
                    if (!found_next_page) {
                        next_page = PAGE_MSG_CW_1;
                    }
                    break;
                default:
                    next_page = PAGE_MSG_CW_1;
                    break;
            }
            break;
        case GROUP_MSG_VOICE:
            switch (buttons_page) {
                case PAGE_MSG_VOICE_1:
                case PAGE_MSG_VOICE_2:
                    if (!found_next_page) {
                        next_page = PAGE_MSG_VOICE_1;
                    }
                    break;
                default:
                    next_page = PAGE_MSG_VOICE_1;
                    break;
            }
            break;
        case GROUP_DFN:
            switch (buttons_page) {
                case PAGE_DFN_1:
                case PAGE_DFN_2:
                case PAGE_DFN_3:
                    if (!found_next_page) {
                        next_page = PAGE_DFN_1;
                    }
                    break;
                default:
                    next_page = PAGE_DFN_1;
                    break;
            }
            break;
        default:
            LV_LOG_ERROR("Unknown page group: %u", group);
            return;
    }
    buttons_unload_page();
    buttons_load_page(next_page);
}

static char * vol_label_getter() {
    static char buf[16];
    sprintf(buf, "Volume:\n%zi", params.vol);
    return buf;
}

static char * sql_label_getter() {
    static char buf[16];
    sprintf(buf, "Squelch:\n%zu", params.sql);
    return buf;
}

static char * rfg_label_getter() {
    static char buf[16];
    sprintf(buf, "RF gain:\n%zu", params_band_rfg_get());
    return buf;
}

static char * tx_power_label_getter() {
    static char buf[20];
    sprintf(buf, "TX power:\n%0.1f W", params.pwr);
    return buf;
}

static char * filter_low_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter low:\n%zu Hz", params_current_mode_filter_low_get());
    return buf;
}
static char * filter_high_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter high:\n%zu Hz", params_current_mode_filter_high_get());
    return buf;
}

static char * filter_bw_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter BW:\n%zu Hz", params_current_mode_filter_bw_get());
    return buf;
}


static char * speaker_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "SP mode:\n%s", params.spmode.x ? "Speaker": "Phones");
    return buf;
}


static char * mic_sel_label_getter() {
    static char buf[22];
    sprintf(buf, "MIC Sel:\n%s", params_mic_str_get(params.mic));
    return buf;
}


static char * h_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "H-Mic gain:\n%zu", params.hmic);
    return buf;
}

static char * i_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "I-Mic gain:\n%zu", params.imic);
    return buf;
}

static char * moni_level_label_getter() {
    static char buf[22];
    sprintf(buf, "Moni level:\n%zu", params.moni);
    return buf;
}

static char * charger_label_getter() {
    static char buf[22];
    sprintf(buf, "Charger:\n%s", params_charger_str_get(params.charger));
    return buf;
}


static char * rit_label_getter() {
    static char buf[22];
    sprintf(buf, "RIT:\n%+zi", params.rit);
    return buf;
}

static char * xit_label_getter() {
    static char buf[22];
    sprintf(buf, "XIT:\n%+zi", params.xit);
    return buf;
}

static char * agc_hang_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC hang:\n%s", params.agc_hang ? "On": "Off");
    return buf;
}

static char * agc_knee_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC knee:\n%zi dB", params.agc_knee);
    return buf;
}

static char * agc_slope_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC slope:\n%zu dB", params.agc_slope);
    return buf;
}

static char * key_speed_label_getter() {
    static char buf[22];
    sprintf(buf, "Speed:\n%zu wpm", params.key_speed);
    return buf;
}

static char * key_volume_label_getter() {
    static char buf[22];
    sprintf(buf, "Volume:\n%zu", params.key_vol);
    return buf;
}

static char * key_train_label_getter() {
    static char buf[22];
    sprintf(buf, "Train:\n%s", params.key_train ? "On": "Off");
    return buf;
}

static char * key_tone_label_getter() {
    static char buf[22];
    sprintf(buf, "Tone:\n%zu Hz", subject_get_int(cfg.key_tone.val));
    return buf;
}

static char * key_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Mode:\n%s", params_key_mode_str_get(params.key_mode));
    return buf;
}

static char * iambic_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Iambic:\n%s mode", params_iambic_mode_str_ger(params.iambic_mode));
    return buf;
}

static char * qsk_time_label_getter() {
    static char buf[22];
    sprintf(buf, "QSK time:\n%zu ms", params.qsk_time);
    return buf;
}

static char * key_ratio_label_getter() {
    static char buf[22];
    sprintf(buf, "Ratio:\n%0.1f", (float)params.key_ratio / 10.0f);
    return buf;
}

static char * cw_decoder_label_getter() {
    static char buf[22];
    sprintf(buf, "Decoder:\n%s", params.cw_decoder ? "On": "Off");
    return buf;
}

static char * cw_tuner_label_getter() {
    static char buf[22];
    sprintf(buf, "Tuner:\n%s", params.cw_tune ? "On": "Off");
    return buf;
}

static char * cw_snr_label_getter() {
    static char buf[22];
    sprintf(buf, "Dec SNR:\n%0.1f dB", params.cw_decoder_snr);
    return buf;
}

static char * cw_peak_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Peak beta:\n%0.2f", params.cw_decoder_peak_beta);
    return buf;
}

static char * cw_noise_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Noise beta:\n%0.2f", params.cw_decoder_noise_beta);
    return buf;
}

static char * dnf_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF:\n%s", params.dnf ? "On": "Off");
    return buf;
}

static char * dnf_center_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF freq:\n%zu Hz", params.dnf_center);
    return buf;
}

static char * dnf_width_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF width:\n%zu Hz", params.dnf_width);
    return buf;
}

static char * nb_label_getter() {
    static char buf[22];
    sprintf(buf, "NB:\n%s", params.nb ? "On": "Off");
    return buf;
}

static char * nb_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NB level:\n%zu", params.nb_level);
    return buf;
}

static char * nb_width_label_getter() {
    static char buf[22];
    sprintf(buf, "NB width:\n%zu Hz", params.nb_width);
    return buf;
}

static char * nr_label_getter() {
    static char buf[22];
    sprintf(buf, "NR:\n%s", params.nr ? "On": "Off");
    return buf;
}

static char * nr_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NR level:\n%zu", params.nr_level);
    return buf;
}



static void param_changed_cb(void * s, lv_msg_t * m) {
    for (size_t i = 0; i < BUTTONS; i++) {
        lv_obj_t  *label = lv_obj_get_user_data(btn[i].obj);
        if (!label) continue;
        label_cb_fn label_getter = lv_obj_get_user_data(label);
        if (!label_getter) continue;
        lv_label_set_text(label, label_getter());
    }
}
