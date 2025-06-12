/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "buttons.h"

#include "controls.h"

#include <stdio.h>
#include <string>
#include <vector>

extern "C" {
    #include "styles.h"
    #include "main_screen.h"
    #include "mfk.h"
    #include "vol.h"
    #include "msg.h"
    #include "pannel.h"
    #include "params/params.h"
    #include "voice.h"
    #include "pubsub_ids.h"
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

typedef struct {
    lv_obj_t        *label;
    button_item_t   *item;
} button_t;

static button_t       btn[BUTTONS];
static lv_obj_t      *parent_obj = NULL;
static buttons_page_t *cur_page   = NULL;

static void button_app_page_cb(button_item_t *item);
static void button_vol_update_cb(button_item_t *item);
static void button_mfk_update_cb(button_item_t *item);
static void button_mem_load_cb(button_item_t *item);

static void param_changed_cb(void * s, lv_msg_t * m);
static void label_update_cb(Subject *subj, void *user_data);

static void button_vol_hold_cb(button_item_t *item);
static void button_mfk_hold_cb(button_item_t *item);
static void button_mem_save_cb(button_item_t *item);

// Label getters

static const char * vol_label_getter();
static const char * sql_label_getter();
static const char * rfg_label_getter();
static const char * tx_power_label_getter();

static const char * filter_low_label_getter();
static const char * filter_high_label_getter();
static const char * filter_bw_label_getter();
static const char * speaker_mode_label_getter();

static const char * mic_sel_label_getter();
static const char * h_mic_gain_label_getter();
static const char * i_mic_gain_label_getter();
static const char * moni_level_label_getter();

static const char * charger_label_getter();
static const char * rit_label_getter();
static const char * xit_label_getter();

static const char * agc_hang_label_getter();
static const char * agc_knee_label_getter();
static const char * agc_slope_label_getter();
static const char * comp_label_getter();

static const char * key_speed_label_getter();
static const char * key_volume_label_getter();
static const char * key_train_label_getter();
static const char * key_tone_label_getter();

static const char * key_mode_label_getter();
static const char * iambic_mode_label_getter();
static const char * qsk_time_label_getter();
static const char * key_ratio_label_getter();

static const char * cw_decoder_label_getter();
static const char * cw_tuner_label_getter();
static const char * cw_snr_label_getter();

static const char * cw_peak_beta_label_getter();
static const char * cw_noise_beta_label_getter();

static const char * dnf_label_getter();
static const char * dnf_center_label_getter();
static const char * dnf_width_label_getter();
static const char * dnf_auto_label_getter();

static const char * nb_label_getter();
static const char * nb_level_label_getter();
static const char * nb_width_label_getter();

static const char * nr_label_getter();
static const char * nr_level_label_getter();

static void button_action_cb(button_item_t *item);

/* Make VOL button functions */
static button_item_t make_btn(const char *name, vol_mode_t data) {
    return button_item_t{
        .type = BTN_TEXT, .label = name, .press = button_vol_update_cb, .hold = button_vol_hold_cb, .data = data};
}

static button_item_t make_btn(const char *(*label_fn)(), vol_mode_t data, Subject **subj = nullptr) {
    return button_item_t{.type     = BTN_TEXT_FN,
                         .label_fn = label_fn,
                         .press    = button_vol_update_cb,
                         .hold     = button_vol_hold_cb,
                         .data     = data,
                         .subj     = subj};
}

/* Make MFK button functions */
static button_item_t make_btn(const char *name, mfk_mode_t data) {
    return button_item_t{
        .type = BTN_TEXT, .label = name, .press = button_mfk_update_cb, .hold = button_mfk_hold_cb, .data = data};
}

static button_item_t make_btn(const char *(*label_fn)(), mfk_mode_t data, Subject **subj = nullptr) {
    return button_item_t{.type     = BTN_TEXT_FN,
                         .label_fn = label_fn,
                         .press    = button_mfk_update_cb,
                         .hold     = button_mfk_hold_cb,
                         .data     = data,
                         .subj     = subj};
}

/* Make MEM buttons functions */
static button_item_t make_mem_btn(const char *name, int32_t data) {
    return button_item_t{
        .type = BTN_TEXT, .label = name, .press = button_mem_load_cb, .hold = button_mem_save_cb, .data = data};
}

static button_item_t make_app_btn(const char *name, press_action_t data) {
    return button_item_t{.type = BTN_TEXT, .label = name, .press = button_app_page_cb, .hold = nullptr, .data = data};
}
static button_item_t make_action_btn(const char *name, press_action_t data) {
    return button_item_t{.type = BTN_TEXT, .label = name, .press = button_action_cb, .hold = nullptr, .data = data};
}

static button_item_t make_page_btn(const char *name, const char *voice) {
    return button_item_t{
        .type = BTN_TEXT, .label = name, .press = button_next_page_cb, .hold = button_prev_page_cb, .voice = voice};
}

/* VOL page 1 */

static button_item_t btn_vol = {
    .type     = BTN_TEXT_FN,
    .label_fn = vol_label_getter,
    .press    = button_vol_update_cb,
    .data     = VOL_VOL,
    .subj     = &cfg.vol.val,
};

static button_item_t btn_sql = make_btn(sql_label_getter, VOL_SQL, &cfg.sql.val);
static button_item_t btn_rfg = make_btn(rfg_label_getter, VOL_RFG);
static button_item_t btn_tx_pwr = make_btn(tx_power_label_getter, VOL_PWR, &cfg.pwr.val);

/* VOL page 2 */

static button_item_t btn_flt_low  = make_btn(filter_low_label_getter, VOL_FILTER_LOW, &cfg_cur.filter.low);
static button_item_t btn_flt_high = make_btn(filter_high_label_getter, VOL_FILTER_HIGH, &cfg_cur.filter.high);
static button_item_t btn_flt_bw   = make_btn(filter_bw_label_getter, VOL_FILTER_BW, &cfg_cur.filter.bw);

/* VOL page 3 */

static button_item_t btn_mic_sel   = make_btn(mic_sel_label_getter, VOL_MIC);
static button_item_t btn_hmic_gain = make_btn(h_mic_gain_label_getter, VOL_HMIC);
static button_item_t btn_imic_hain = make_btn(i_mic_gain_label_getter, VOL_IMIC);
static button_item_t btn_moni_lvl  = make_btn(moni_level_label_getter, VOL_MONI);

/* VOL page 4 */

static button_item_t btn_voice       = make_btn("Voice", VOL_VOICE_LANG);
static button_item_t btn_voice_rate  = make_btn("Voice\nRate", VOL_VOICE_RATE);
static button_item_t btn_voice_pitch = make_btn("Voice\nPitch", VOL_VOICE_PITCH);
static button_item_t btn_voice_vol   = make_btn("Voice\nVolume", VOL_VOICE_VOLUME);

/* MFK page 1 */

static button_item_t btn_spectrum_min_level = make_btn("Min\nLevel", MFK_MIN_LEVEL);
static button_item_t btn_spectrum_max_level = make_btn("Max\nLevel", MFK_MAX_LEVEL);
static button_item_t btn_zoom               = make_btn("Spectrum\nZoom", MFK_SPECTRUM_FACTOR);
static button_item_t btn_spectrum_beta      = make_btn("Spectrum\nBeta", MFK_SPECTRUM_BETA);

/* MFK page 2 */

static button_item_t btn_spectrum_fill  = make_btn("Spectrum\nFill", MFK_SPECTRUM_FILL);
static button_item_t btn_spectrum_peak  = make_btn("Spectrum\nPeak", MFK_SPECTRUM_PEAK);
static button_item_t btn_spectrum_hold  = make_btn("Peaks\nHold", MFK_PEAK_HOLD);
static button_item_t btn_spectrum_speed = make_btn("Peaks\nSpeed", MFK_PEAK_SPEED);

/* MFK page 3 */
static button_item_t btn_charger = make_btn(charger_label_getter, MFK_CHARGER);
static button_item_t btn_ant     = make_btn("Antenna", MFK_ANT);
static button_item_t btn_rit     = make_btn(rit_label_getter, MFK_RIT);
static button_item_t btn_xit     = make_btn(xit_label_getter, MFK_XIT);

/* MFK page 4 */

static button_item_t btn_agc_hang  = {.type     = BTN_TEXT_FN,
                                      .label_fn = agc_hang_label_getter,
                                      .press    = controls_toggle_agc_hang,
                                      .hold     = button_mfk_hold_cb,
                                      .data     = MFK_AGC_HANG,
                                      .subj     = &cfg.agc_hang.val};
static button_item_t btn_agc_knee  = make_btn(agc_knee_label_getter, MFK_AGC_KNEE, &cfg.agc_knee.val);
static button_item_t btn_agc_slope = make_btn(agc_slope_label_getter, MFK_AGC_SLOPE, &cfg.agc_slope.val);
static button_item_t btn_comp      = make_btn(comp_label_getter, MFK_COMP, &cfg.comp.val);

/* MEM page 1 */

static button_item_t btn_mem_1 = make_mem_btn("Set 1", 1);
static button_item_t btn_mem_2 = make_mem_btn("Set 2", 2);
static button_item_t btn_mem_3 = make_mem_btn("Set 3", 3);
static button_item_t btn_mem_4 = make_mem_btn("Set 4", 4);

/* MEM page 2 */

static button_item_t btn_mem_5 = make_mem_btn("Set 5", 5);
static button_item_t btn_mem_6 = make_mem_btn("Set 6", 6);
static button_item_t btn_mem_7 = make_mem_btn("Set 7", 7);
static button_item_t btn_mem_8 = make_mem_btn("Set 8", 8);

/* CW */

static button_item_t btn_key_speed  = make_btn(key_speed_label_getter, MFK_KEY_SPEED, &cfg.key_speed.val);
static button_item_t btn_key_volume = make_btn(key_volume_label_getter, MFK_KEY_VOL, &cfg.key_vol.val);
static button_item_t btn_key_train  = {.type     = BTN_TEXT_FN,
                                       .label_fn = key_train_label_getter,
                                       .press    = controls_toggle_key_train,
                                       .hold     = button_mfk_hold_cb,
                                       .data     = MFK_KEY_TRAIN,
                                       .subj     = &cfg.key_train.val};
static button_item_t btn_key_tone   = make_btn(key_tone_label_getter, MFK_KEY_TONE, &cfg.key_tone.val);

static button_item_t btn_key_mode        = make_btn(key_mode_label_getter, MFK_KEY_MODE, &cfg.key_mode.val);
static button_item_t btn_key_iambic_mode = {.type     = BTN_TEXT_FN,
                                            .label_fn = iambic_mode_label_getter,
                                            .press    = controls_toggle_key_iambic_mode,
                                            .hold     = button_mfk_hold_cb,
                                            .data     = MFK_IAMBIC_MODE,
                                            .subj     = &cfg.iambic_mode.val};
static button_item_t btn_key_qsk_time    = make_btn(qsk_time_label_getter, MFK_QSK_TIME, &cfg.qsk_time.val);
static button_item_t btn_key_ratio       = make_btn(key_ratio_label_getter, MFK_KEY_RATIO, &cfg.key_ratio.val);

static button_item_t btn_cw_decoder = {.type     = BTN_TEXT_FN,
                                       .label_fn = cw_decoder_label_getter,
                                       .press    = controls_toggle_cw_decoder,
                                       .hold     = button_mfk_hold_cb,
                                       .data     = MFK_CW_DECODER,
                                       .subj     = &cfg.cw_decoder.val};
static button_item_t btn_cw_tuner   = {.type     = BTN_TEXT_FN,
                                       .label_fn = cw_tuner_label_getter,
                                       .press    = controls_toggle_cw_tuner,
                                       .hold     = button_mfk_hold_cb,
                                       .data     = MFK_CW_TUNE,
                                       .subj     = &cfg.cw_tune.val};
static button_item_t btn_cw_snr     = make_btn(cw_snr_label_getter, MFK_CW_DECODER_SNR, &cfg.cw_decoder_snr.val);
static button_item_t btn_cw_peak_beta =
    make_btn(cw_peak_beta_label_getter, MFK_CW_DECODER_PEAK_BETA, &cfg.cw_decoder_peak_beta.val);
static button_item_t btn_cw_noise_beta =
    make_btn(cw_noise_beta_label_getter, MFK_CW_DECODER_NOISE_BETA, &cfg.cw_decoder_noise_beta.val);

/* DSP */

static button_item_t btn_dnf        = {.type     = BTN_TEXT_FN,
                                       .label_fn = dnf_label_getter,
                                       .press    = controls_toggle_dnf,
                                       .hold     = button_mfk_hold_cb,
                                       .data     = MFK_DNF,
                                       .subj     = &cfg.dnf.val};
static button_item_t btn_dnf_center = make_btn(dnf_center_label_getter, MFK_DNF_CENTER, &cfg.dnf_center.val);
static button_item_t btn_dnf_width  = make_btn(dnf_width_label_getter, MFK_DNF_WIDTH, &cfg.dnf_width.val);
static button_item_t btn_dnf_auto   = {.type     = BTN_TEXT_FN,
                                       .label_fn = dnf_auto_label_getter,
                                       .press    = controls_toggle_dnf_auto,
                                       .hold     = button_mfk_hold_cb,
                                       .data     = MFK_DNF_AUTO,
                                       .subj     = &cfg.dnf_auto.val};


static button_item_t btn_nb       = {.type     = BTN_TEXT_FN,
                                     .label_fn = nb_label_getter,
                                     .press    = controls_toggle_nb,
                                     .hold     = button_mfk_hold_cb,
                                     .data     = MFK_NB,
                                     .subj     = &cfg.nb.val};
static button_item_t btn_nb_level = make_btn(nb_level_label_getter, MFK_NB_LEVEL, &cfg.nb_level.val);
static button_item_t btn_nb_width = make_btn(nb_width_label_getter, MFK_NB_WIDTH, &cfg.nb_width.val);

static button_item_t btn_nr       = {.type     = BTN_TEXT_FN,
                                     .label_fn = nr_label_getter,
                                     .press    = controls_toggle_nr,
                                     .hold     = button_mfk_hold_cb,
                                     .data     = MFK_NR,
                                     .subj     = &cfg.nr.val};
static button_item_t btn_nr_level = make_btn(nr_level_label_getter, MFK_NR_LEVEL, &cfg.nr_level.val);

/* APP */

static button_item_t btn_rtty = make_app_btn("RTTY", ACTION_APP_RTTY);
static button_item_t btn_ft8  = make_app_btn("FT8", ACTION_APP_FT8);
static button_item_t btn_swr  = make_app_btn("SWR\nScan", ACTION_APP_SWRSCAN);
static button_item_t btn_gps  = make_app_btn("GPS", ACTION_APP_GPS);

static button_item_t btn_rec      = make_app_btn("Recorder", ACTION_APP_RECORDER);
static button_item_t btn_qth      = make_action_btn("QTH", ACTION_APP_QTH);
static button_item_t btn_callsign = make_action_btn("Callsign", ACTION_APP_CALLSIGN);
static button_item_t btn_settings = make_app_btn("Settings", ACTION_APP_SETTINGS);

static button_item_t  btn_wifi   = make_app_btn("WiFi", ACTION_APP_WIFI);

/* RTTY */
static button_item_t btn_rtty_p1 = {
    .type  = BTN_TEXT,
    .label = "(RTTY 1:1)",
    .press = NULL,
};
static button_item_t btn_rtty_rate = {
    .type  = BTN_TEXT,
    .label = "Rate",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_RATE,
};
static button_item_t btn_rtty_shift = {
    .type  = BTN_TEXT,
    .label = "Shift",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_SHIFT,
};
static button_item_t btn_rtty_center = {
    .type  = BTN_TEXT,
    .label = "Center",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_CENTER,
};
static button_item_t btn_rtty_reverse = {
    .type  = BTN_TEXT,
    .label = "Reverse",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_REVERSE,
};


/* VOL pages */
static button_item_t btn_vol_p1 = make_page_btn("(VOL 1:3)", "Volume|page 1");
static button_item_t btn_vol_p2 = make_page_btn("(VOL 2:3)", "Volume|page 2");
static button_item_t btn_vol_p3 = make_page_btn("(VOL 3:3)", "Volume|page 3");

buttons_page_t buttons_page_vol_1 = {
    {&btn_vol_p1, &btn_vol, &btn_sql, &btn_rfg, &btn_tx_pwr}
};
static buttons_page_t page_vol_2 = {
    {&btn_vol_p2, &btn_mic_sel, &btn_hmic_gain, &btn_imic_hain, &btn_moni_lvl}
};
static buttons_page_t page_vol_3 = {
    {&btn_vol_p3, &btn_voice, &btn_voice_rate, &btn_voice_pitch, &btn_voice_vol}
};

/* MFK pages */
static button_item_t btn_mfk_p1 = make_page_btn("(MFK 1:4)", "MFK|page 1");
static button_item_t btn_mfk_p2 = make_page_btn("(MFK 2:4)", "MFK|page 2");
static button_item_t btn_mfk_p3 = make_page_btn("(MFK 3:4)", "MFK|page 3");
static button_item_t btn_mfk_p4 = make_page_btn("(MFK 4:4)", "MFK|page 4");

static buttons_page_t page_mfk_1 = {
    {&btn_mfk_p1, &btn_spectrum_min_level, &btn_spectrum_max_level, &btn_zoom, &btn_spectrum_beta}
};
static buttons_page_t page_mfk_2 = {
    {&btn_mfk_p2, &btn_spectrum_fill, &btn_spectrum_peak, &btn_spectrum_hold, &btn_spectrum_speed}
};
static buttons_page_t page_mfk_3 = {
    {&btn_mfk_p3, &btn_charger, &btn_ant, &btn_rit, &btn_xit}
};
static buttons_page_t page_mfk_4 = {
    {&btn_mfk_p4, &btn_agc_hang, &btn_agc_knee, &btn_agc_slope, &btn_comp}
};

/* MEM pages */

static button_item_t btn_mem_p1 = make_page_btn("(MEM 1:2)", "Memory|page 1");
static button_item_t btn_mem_p2 = make_page_btn("(MEM 2:2)", "Memory|page 2");

static buttons_page_t page_mem_1 = {
    {&btn_mem_p1, &btn_mem_1, &btn_mem_2, &btn_mem_3, &btn_mem_4}
};
static buttons_page_t page_mem_2 = {
    {&btn_mem_p2, &btn_mem_5, &btn_mem_6, &btn_mem_7, &btn_mem_8}
};

/* KEY pages */
static button_item_t btn_key_p1 = make_page_btn("(KEY 1:2)", "Key|page 1");
static button_item_t btn_key_p2 = make_page_btn("(KEY 2:2)", "Key|page 2");
static button_item_t btn_cw_p1  = make_page_btn("(CW 1:2)", "CW|page 1");
static button_item_t btn_cw_p2  = make_page_btn("(CW 2:2)", "CW|page 2");

static buttons_page_t page_key_1 = {
    {&btn_key_p1, &btn_key_speed, &btn_key_volume, &btn_key_train, &btn_key_tone}
};
static buttons_page_t page_key_2 = {
    {&btn_key_p2, &btn_key_mode, &btn_key_iambic_mode, &btn_key_qsk_time, &btn_key_ratio}
};
static buttons_page_t page_cw_decoder_1 = {
    {&btn_cw_p1, &btn_cw_decoder, &btn_cw_tuner, &btn_cw_snr}
};
static buttons_page_t page_cw_decoder_2 = {
    {&btn_cw_p2, &btn_cw_peak_beta, &btn_cw_noise_beta}
};

/* DFN pages */
static button_item_t btn_dfn_p1 = make_page_btn("(DFN 1:3)", "DNF page");
static button_item_t btn_dfn_p2 = make_page_btn("(DFN 2:3)", "NB page");
static button_item_t btn_dfn_p3 = make_page_btn("(DFN 3:3)", "NR page");

static buttons_page_t page_dfn_1 = {
    {&btn_dfn_p1, &btn_dnf, &btn_dnf_center, &btn_dnf_width, &btn_dnf_auto}
};
static buttons_page_t page_dfn_2 = {
    {&btn_dfn_p2, &btn_nb, &btn_nb_level, &btn_nb_width}
};
static buttons_page_t page_dfn_3 = {
    {&btn_dfn_p3, &btn_nr, &btn_nr_level}
};

/* DFL pages */
static buttons_page_t page_dfl_1 = {
    {&btn_flt_low, &btn_flt_high, &btn_flt_bw}
};

/* App pages */
static button_item_t btn_app_p1 = make_page_btn("(APP 1:3)", "Application|page 1");
static button_item_t btn_app_p2 = make_page_btn("(APP 2:3)", "Application|page 2");
static button_item_t btn_app_p3 = make_page_btn("(APP 3:3)", "Application|page 3");

static buttons_page_t page_app_1 = {
    {&btn_app_p1, &btn_rtty, &btn_ft8, &btn_swr, &btn_gps}
};
static buttons_page_t page_app_2 = {
    {&btn_app_p2, &btn_rec, &btn_qth, &btn_callsign, &btn_settings}
};
static buttons_page_t page_app_3 = {
    {&btn_app_p3, &btn_wifi}
};

/* RTTY */

buttons_page_t buttons_page_rtty = {
    {&btn_rtty_p1, &btn_rtty_rate, &btn_rtty_shift, &btn_rtty_center, &btn_rtty_reverse}
};

buttons_group_t buttons_group_gen = {
    &buttons_page_vol_1,
    &page_vol_2,
    &page_vol_3,
    &page_mfk_1,
    &page_mfk_2,
    &page_mfk_3,
    &page_mfk_4,
};

buttons_group_t buttons_group_app = {
    &page_app_1,
    &page_app_2,
    &page_app_3,
};

buttons_group_t buttons_group_key = {
    &page_key_1,
    &page_key_2,
    &page_cw_decoder_1,
    &page_cw_decoder_2,
};

buttons_group_t buttons_group_dfn = {
    &page_dfn_1,
    &page_dfn_2,
    &page_dfn_3,
};

buttons_group_t buttons_group_dfl = {
    &page_dfl_1,
};

buttons_group_t buttons_group_vm = {
    &page_mem_1,
    &page_mem_2,
};

static struct {
    buttons_page_t **group;
    size_t           size;
} groups[] = {
    {buttons_group_gen, ARRAY_SIZE(buttons_group_gen)},
    {buttons_group_app, ARRAY_SIZE(buttons_group_app)},
    {buttons_group_key, ARRAY_SIZE(buttons_group_key)},
    {buttons_group_dfn, ARRAY_SIZE(buttons_group_dfn)},
    {buttons_group_dfl, ARRAY_SIZE(buttons_group_dfl)},
    {buttons_group_vm,  ARRAY_SIZE(buttons_group_vm) },
};

void buttons_init(lv_obj_t *parent) {

    if (x6100_control_get_patched_revision() < 3) {
        // Hide DNF auto button
        page_dfn_1.items[4] = NULL;
    }

    btn_rfg.subj = &cfg_cur.band->rfg.val;

    /* Fill prev/next pointers */
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        buttons_page_t **group = groups[i].group;
        for (size_t j = 0; j < groups[i].size; j++) {
            if (group[j]->items[0]->press == button_next_page_cb) {
                uint16_t next_id = (j + 1) % groups[i].size;
                group[j]->items[0]->next = group[next_id];
            } else {
                LV_LOG_USER("First button in page=%u, group=%u press cb is not next", j, i);
            }
            if (group[j]->items[0]->hold == button_prev_page_cb) {
                uint16_t prev_id = (groups[i].size + j - 1) % groups[i].size;
                group[j]->items[0]->prev = group[prev_id];
            } else {
                LV_LOG_USER("First button in page=%u, group=%u hold cb is not prev", j, i);
            }
        }
    }

    uint16_t y = 480 - BTN_HEIGHT;
    uint16_t x = 0;
    uint16_t width = 800 / 5;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_obj_create(parent);

        lv_obj_remove_style_all(f);
        lv_obj_add_style(f, &btn_style, 0);
        lv_obj_add_style(f, &btn_active_style, LV_STATE_CHECKED);
        lv_obj_add_style(f, &btn_disabled_style, LV_STATE_DISABLED);

        lv_obj_set_pos(f, x, y);
        lv_obj_set_size(f, width, BTN_HEIGHT);
        x += width;

        lv_obj_t *label = lv_label_create(f);

        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        btn[i].label = label;
    }

    parent_obj = parent;
    lv_msg_subscribe(MSG_PARAM_CHANGED, param_changed_cb, NULL);
}

void buttons_refresh(button_item_t *item) {
    if (item->label_obj) {
        if (item->type == BTN_TEXT) {
            lv_label_set_text(item->label_obj, item->label);
        } else if (item->type == BTN_TEXT_FN) {
            lv_label_set_text(item->label_obj, item->label_fn());
        } else {
            lv_label_set_text(item->label_obj, "--");
        }

    } else {
        LV_LOG_WARN("Button item label obj is null");
    }
}

void buttons_mark(button_item_t *item, bool val) {
    item->mark = val;
    if (item->label_obj) {
        lv_obj_t *btn = lv_obj_get_parent(item->label_obj);
        if (val) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);

        } else {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
    }
}

void buttons_disabled(button_item_t *item, bool val) {
    item->disabled = val;
    if (item->label_obj) {
        lv_obj_t *btn = lv_obj_get_parent(item->label_obj);
        if (val) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);

        } else {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
        }
    }
}

void buttons_load(uint8_t n, button_item_t *item) {
    button_item_t *prev_item = btn[n].item;
    if (prev_item) {
        prev_item->label_obj = NULL;
        if (prev_item->observer) {
            delete prev_item->observer;
            prev_item->observer = NULL;
        }
    }

    lv_obj_t *label = btn[n].label;
    if (item) {
        if (item->type == BTN_TEXT) {
            lv_label_set_text(label, item->label);
        } else if (item->type == BTN_TEXT_FN) {
            lv_label_set_text(label, item->label_fn());
            if (item->subj && *item->subj) {
                item->observer = (*item->subj)->subscribe_delayed(label_update_cb, item);
            } else {
                lv_obj_set_user_data(label, (void *)item->label_fn);
            }
        } else {
            lv_label_set_text(label, "");
        }
        item->label_obj = label;
        lv_obj_t *btn = lv_obj_get_parent(label);
        if (item->mark) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
        if (item->disabled) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
        }
    } else {
        lv_label_set_text(label, "");
    }
    btn[n].item = item;
}

void buttons_load_page(buttons_page_t *page) {
    if (!page) {
        LV_LOG_ERROR("NULL pointer to buttons page");
        return;
    }
    if (cur_page) {
        buttons_unload_page();
    }
    cur_page = page;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        buttons_load(i, page->items[i]);
    }
    if (page->items[0]->voice) {
        voice_say_text_fmt("%s", page->items[0]->voice);
    }
}

void buttons_unload_page() {
    cur_page = NULL;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t        *label = btn[i].label;
        lv_label_set_text(label, "");
        lv_obj_set_user_data(label, NULL);
        lv_obj_clear_state(lv_obj_get_parent(label), LV_STATE_CHECKED);
        lv_obj_clear_state(lv_obj_get_parent(label), LV_STATE_DISABLED);
        if (btn[i].item) {
            btn[i].item->label_obj = NULL;
            if (btn[i].item->observer) {
                delete btn[i].item->observer;
                btn[i].item->observer = NULL;
            }
            btn[i].item = NULL;
        }
    }
}

void button_next_page_cb(button_item_t *item) {
    buttons_unload_page();
    buttons_load_page(item->next);
}

void button_prev_page_cb(button_item_t *item) {
    buttons_unload_page();
    buttons_load_page(item->prev);
}

static void button_app_page_cb(button_item_t *item) {
    main_screen_start_app((press_action_t)item->data);
}

static void button_action_cb(button_item_t *item) {
    main_screen_action((press_action_t)item->data);
}

static void button_vol_update_cb(button_item_t *item) {
    vol_set_mode((vol_mode_t)item->data);
    vol_update(0, true, true);
}

static void button_mfk_update_cb(button_item_t *item) {
    mfk_set_mode((mfk_mode_t)item->data);
    mfk_update(0, true, true);
}

static void button_vol_hold_cb(button_item_t *item) {
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

static void button_mfk_hold_cb(button_item_t *item) {
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

static void button_mem_load_cb(button_item_t *item) {
    mem_load(item->data);
    voice_say_text_fmt("Memory %i loaded", item->data);
}

static void button_mem_save_cb(button_item_t *item) {
    mem_save(item->data);
    voice_say_text_fmt("Memory %i stored", item->data);
}

void buttons_press(uint8_t n, bool hold) {
    button_item_t *item = btn[n].item;
    if (item == NULL) {
        LV_LOG_WARN("Button %u is NULL", n);
        return;
    }
    if (item->disabled) {
        LV_LOG_USER("Button %s disabled", lv_label_get_text(item->label_obj));
        return;
    }
    if (hold) {
        if (item->hold) {
            item->hold(item);
        } else {
            LV_LOG_USER("Button %u hold action is NULL", n);
        }
    } else {
        if (item->press) {
            item->press(item);
        } else {
            LV_LOG_USER("Button %u press action is NULL", n);
        }
    }
}

void buttons_load_page_group(buttons_group_t group) {
    size_t group_size = 0;
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        if (groups[i].group == group) {
            group_size = groups[i].size;
            break;
        }
    }
    if (group_size <= 0) {
        return;
    }
    for (size_t i = 0; i < group_size; i++) {
        if ((group[i] == cur_page) && (cur_page->items[0]->next)) {
            // load next
            cur_page->items[0]->press(cur_page->items[0]);
            return;
        }
    }
    // Load first
    buttons_load_page(group[0]);
}

buttons_page_t *buttons_get_cur_page() {
    return cur_page;
}

static const char * vol_label_getter() {
    static char buf[16];
    sprintf(buf, "Volume:\n%zi", subject_get_int(cfg.vol.val));
    return buf;
}

static const char * sql_label_getter() {
    static char buf[16];
    sprintf(buf, "Squelch:\n%zu", subject_get_int(cfg.sql.val));
    return buf;
}

static const char * rfg_label_getter() {
    static char buf[16];
    sprintf(buf, "RF gain:\n%zu", subject_get_int(cfg_cur.band->rfg.val));
    return buf;
}

static const char * tx_power_label_getter() {
    static char buf[20];
    sprintf(buf, "TX power:\n%0.1f W", subject_get_float(cfg.pwr.val));
    return buf;
}

static const char * filter_low_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter low:\n%zu Hz", subject_get_int(cfg_cur.filter.low));
    return buf;
}
static const char * filter_high_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter high:\n%zu Hz", subject_get_int(cfg_cur.filter.high));
    return buf;
}

static const char * filter_bw_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter BW:\n%i Hz", subject_get_int(cfg_cur.filter.bw));
    return buf;
}


static const char * mic_sel_label_getter() {
    static char buf[22];
    sprintf(buf, "MIC Sel:\n%s", params_mic_str_get(params.mic));
    return buf;
}


static const char * h_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "H-Mic gain:\n%zu", params.hmic);
    return buf;
}

static const char * i_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "I-Mic gain:\n%zu", params.imic);
    return buf;
}

static const char * moni_level_label_getter() {
    static char buf[22];
    sprintf(buf, "Moni level:\n%zu", params.moni);
    return buf;
}

static const char * charger_label_getter() {
    static char buf[22];
    sprintf(buf, "Charger:\n%s", params_charger_str_get(params.charger));
    return buf;
}


static const char * rit_label_getter() {
    static char buf[22];
    sprintf(buf, "RIT:\n%+zi", params.rit);
    return buf;
}

static const char * xit_label_getter() {
    static char buf[22];
    sprintf(buf, "XIT:\n%+zi", params.xit);
    return buf;
}

static const char * agc_hang_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC hang:\n%s", subject_get_int(cfg.agc_hang.val) ? "On": "Off");
    return buf;
}

static const char * agc_knee_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC knee:\n%zi dB", subject_get_int(cfg.agc_knee.val));
    return buf;
}

static const char * agc_slope_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC slope:\n%zu dB", subject_get_int(cfg.agc_slope.val));
    return buf;
}

static const char * comp_label_getter() {
    static char buf[22];
    sprintf(buf, "Comp:\n%s", params_comp_str_get(subject_get_int(cfg.comp.val)));
    return buf;
}

static const char * key_speed_label_getter() {
    static char buf[22];
    sprintf(buf, "Speed:\n%zu wpm", subject_get_int(cfg.key_speed.val));
    return buf;
}

static const char * key_volume_label_getter() {
    static char buf[22];
    sprintf(buf, "Volume:\n%zu", subject_get_int(cfg.key_vol.val));
    return buf;
}

static const char * key_train_label_getter() {
    static char buf[22];
    sprintf(buf, "Train:\n%s", subject_get_int(cfg.key_train.val) ? "On": "Off");
    return buf;
}

static const char * key_tone_label_getter() {
    static char buf[22];
    sprintf(buf, "Tone:\n%zu Hz", subject_get_int(cfg.key_tone.val));
    return buf;
}

static const char * key_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Mode:\n%s", params_key_mode_str_get((x6100_key_mode_t)subject_get_int(cfg.key_mode.val)));
    return buf;
}

static const char * iambic_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Iambic:\n%s mode", params_iambic_mode_str_ger((x6100_iambic_mode_t)subject_get_int(cfg.iambic_mode.val)));
    return buf;
}

static const char * qsk_time_label_getter() {
    static char buf[22];
    sprintf(buf, "QSK time:\n%zu ms", subject_get_int(cfg.qsk_time.val));
    return buf;
}

static const char * key_ratio_label_getter() {
    static char buf[22];
    sprintf(buf, "Ratio:\n%0.1f", subject_get_float(cfg.key_ratio.val));
    return buf;
}

static const char * cw_decoder_label_getter() {
    static char buf[22];
    sprintf(buf, "Decoder:\n%s", subject_get_int(cfg.cw_decoder.val) ? "On": "Off");
    return buf;
}

static const char * cw_tuner_label_getter() {
    static char buf[22];
    sprintf(buf, "Tuner:\n%s", subject_get_int(cfg.cw_tune.val) ? "On": "Off");
    return buf;
}

static const char * cw_snr_label_getter() {
    static char buf[22];
    sprintf(buf, "Dec SNR:\n%0.1f dB", subject_get_float(cfg.cw_decoder_snr.val));
    return buf;
}

static const char * cw_peak_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Peak beta:\n%0.2f", subject_get_float(cfg.cw_decoder_peak_beta.val));
    return buf;
}

static const char * cw_noise_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Noise beta:\n%0.2f", subject_get_float(cfg.cw_decoder_noise_beta.val));
    return buf;
}

static const char * dnf_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF:\n%s", subject_get_int(cfg.dnf.val) ? "On": "Off");
    return buf;
}

static const char * dnf_center_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF freq:\n%zu Hz", subject_get_int(cfg.dnf_center.val));
    return buf;
}

static const char * dnf_width_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF width:\n%zu Hz", subject_get_int(cfg.dnf_width.val));
    return buf;
}

static const char * dnf_auto_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF auto:\n%s", subject_get_int(cfg.dnf_auto.val) ? "On": "Off");
    return buf;
}

static const char * nb_label_getter() {
    static char buf[22];
    sprintf(buf, "NB:\n%s", subject_get_int(cfg.nb.val) ? "On": "Off");
    return buf;
}

static const char * nb_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NB level:\n%zu", subject_get_int(cfg.nb_level.val));
    return buf;
}

static const char * nb_width_label_getter() {
    static char buf[22];
    sprintf(buf, "NB width:\n%zu Hz", subject_get_int(cfg.nb_width.val));
    return buf;
}

static const char * nr_label_getter() {
    static char buf[22];
    sprintf(buf, "NR:\n%s", subject_get_int(cfg.nr.val) ? "On": "Off");
    return buf;
}

static const char * nr_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NR level:\n%zu", subject_get_int(cfg.nr_level.val));
    return buf;
}


static void param_changed_cb(void * s, lv_msg_t * m) {
    for (size_t i = 0; i < BUTTONS; i++) {
        lv_obj_t  *label = btn[i].label;
        if (!label) continue;
        auto label_getter = (char *(*)(void))lv_obj_get_user_data(label);
        if (!label_getter) continue;
        lv_label_set_text(label, label_getter());
    }
}


static void label_update_cb(Subject *subj, void *user_data) {
    button_item_t *item = (button_item_t*)user_data;
    if (item->label_obj) {
        lv_label_set_text(item->label_obj, item->label_fn());
    } else {
        LV_LOG_WARN("Can't update label: it's NULL");
    }
}
