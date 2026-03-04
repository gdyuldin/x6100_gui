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
#include <array>

extern "C" {
    #include "styles.h"
    #include "main_screen.h"
    #include "mfk.h"
    #include "vol.h"
    #include "msg.h"
    #include "panel.h"
    #include "params/params.h"
    #include "voice.h"
    #include "pubsub_ids.h"
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))
#define STATE_ASSIGNED LV_STATE_USER_1

struct _disp_button_t {
    lv_obj_t      *parent;
    lv_obj_t      *label;
    lv_obj_t      *vol_mark;
    lv_obj_t      *mfk_mark;
    button_item_t *item;  // Link to button item;
};

static disp_btn_t      disp_btns[BUTTONS];
static buttons_page_t *cur_page = NULL;
static std::array<char, CTRL_FAST_ACCESS_LAST> fast_binds;
static std::array<char, CTRL_LAST> binds;

static void disp_btn_refresh(disp_btn_t *b);
static void disp_btn_clear(disp_btn_t *b);

static void button_app_page_cb(button_item_t *item);
static void button_encoder_update_cb(button_item_t *item);
static void button_mem_load_cb(button_item_t *item);

static void encoder_binds_change_cb(Subject *subj, void *user_data);
static void label_update_cb(Subject *subj, void *user_data);

static void button_encoder_hold_update_cb(button_item_t *item);
static void button_mem_save_cb(button_item_t *item);

// Label getters

static const char * vol_label_getter();
static const char * sql_label_getter();
static const char * rfg_label_getter();
static const char * tx_power_label_getter();

static const char * filter_low_label_getter();
static const char * filter_high_label_getter();
static const char * filter_bw_label_getter();

static const char * mic_sel_label_getter();
static const char * h_mic_gain_label_getter();
static const char * i_mic_gain_label_getter();
static const char * moni_level_label_getter();

static const char * rit_label_getter();
static const char * xit_label_getter();

static const char * agc_hang_label_getter();
static const char * agc_knee_label_getter();
static const char * agc_slope_label_getter();
static const char * comp_label_getter();
static const char * if_shift_label_getter();

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

/* Make VOL/MFK button functions */
static button_item_t make_encoder_btn(const char *name, cfg_ctrl_t data) {
    return button_item_t{.type            = BTN_TEXT,
                         .label           = name,
                         .press           = button_encoder_update_cb,
                         .hold            = button_encoder_hold_update_cb,
                         .data            = data,
                         .encoder_allowed = true};
}

static button_item_t make_encoder_btn(const char *(*label_fn)(), cfg_ctrl_t data, Subject **subj = nullptr) {
    return button_item_t{.type            = BTN_TEXT_FN,
                         .label_fn        = label_fn,
                         .press           = button_encoder_update_cb,
                         .hold            = button_encoder_hold_update_cb,
                         .data            = data,
                         .encoder_allowed = true,
                         .subj            = subj};
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

/* VOL */

static button_item_t btn_vol = {
    .type            = BTN_TEXT_FN,
    .label_fn        = vol_label_getter,
    .press           = button_encoder_update_cb,
    .data            = CTRL_VOL,
    .encoder_allowed = true,
    .subj            = &cfg.vol.val,
};

static button_item_t btn_sql = make_encoder_btn(sql_label_getter, CTRL_SQL, &cfg.sql.val);
static button_item_t btn_rfg = make_encoder_btn(rfg_label_getter, CTRL_RFG, &cfg_cur.band->rfg.val);
static button_item_t btn_tx_pwr = make_encoder_btn(tx_power_label_getter, CTRL_PWR, &cfg.pwr.val);
static button_item_t btn_flt_low  = make_encoder_btn(filter_low_label_getter, CTRL_FILTER_LOW, &cfg_cur.filter.low);
static button_item_t btn_flt_high = make_encoder_btn(filter_high_label_getter, CTRL_FILTER_HIGH, &cfg_cur.filter.high);
static button_item_t btn_flt_bw   = make_encoder_btn(filter_bw_label_getter, CTRL_FILTER_BW, &cfg_cur.filter.bw);
static button_item_t btn_mic_sel   = make_encoder_btn(mic_sel_label_getter, CTRL_MIC, &cfg.mic.val);
static button_item_t btn_hmic_gain = make_encoder_btn(h_mic_gain_label_getter, CTRL_HMIC, &cfg.hmic.val);
static button_item_t btn_imic_hain = make_encoder_btn(i_mic_gain_label_getter, CTRL_IMIC, &cfg.imic.val);
static button_item_t btn_moni_lvl  = make_encoder_btn(moni_level_label_getter, CTRL_MONI, &cfg.moni.val);

/* MFK */

static button_item_t btn_zoom      = make_encoder_btn("Spectrum\nZoom", CTRL_SPECTRUM_FACTOR);
static button_item_t btn_ant       = make_encoder_btn("Antenna", CTRL_ANT);
static button_item_t btn_rit       = make_encoder_btn(rit_label_getter, CTRL_RIT, &cfg.rit.val);
static button_item_t btn_xit       = make_encoder_btn(xit_label_getter, CTRL_XIT, &cfg.xit.val);
static button_item_t btn_agc_hang  = {.type            = BTN_TEXT_FN,
                                      .label_fn        = agc_hang_label_getter,
                                      .press           = controls_toggle_agc_hang,
                                      .hold            = button_encoder_hold_update_cb,
                                      .data            = CTRL_AGC_HANG,
                                      .encoder_allowed = true,
                                      .subj            = &cfg.agc_hang.val};
static button_item_t btn_agc_knee  = make_encoder_btn(agc_knee_label_getter, CTRL_AGC_KNEE, &cfg.agc_knee.val);
static button_item_t btn_agc_slope = make_encoder_btn(agc_slope_label_getter, CTRL_AGC_SLOPE, &cfg.agc_slope.val);
static button_item_t btn_comp      = make_encoder_btn(comp_label_getter, CTRL_COMP, &cfg.comp.val);
static button_item_t btn_if_shift  = make_encoder_btn(if_shift_label_getter, CTRL_IF_SHIFT, &cfg_cur.band->if_shift.val);

/* MEM */

static button_item_t btn_mem_1 = make_mem_btn("Set 1", 1);
static button_item_t btn_mem_2 = make_mem_btn("Set 2", 2);
static button_item_t btn_mem_3 = make_mem_btn("Set 3", 3);
static button_item_t btn_mem_4 = make_mem_btn("Set 4", 4);
static button_item_t btn_mem_5 = make_mem_btn("Set 5", 5);
static button_item_t btn_mem_6 = make_mem_btn("Set 6", 6);
static button_item_t btn_mem_7 = make_mem_btn("Set 7", 7);
static button_item_t btn_mem_8 = make_mem_btn("Set 8", 8);

/* CW */

static button_item_t btn_key_speed  = make_encoder_btn(key_speed_label_getter, CTRL_KEY_SPEED, &cfg.key_speed.val);
static button_item_t btn_key_volume = make_encoder_btn(key_volume_label_getter, CTRL_KEY_VOL, &cfg.key_vol.val);
static button_item_t btn_key_train  = {.type            = BTN_TEXT_FN,
                                       .label_fn        = key_train_label_getter,
                                       .press           = controls_toggle_key_train,
                                       .hold            = button_encoder_hold_update_cb,
                                       .data            = CTRL_KEY_TRAIN,
                                       .encoder_allowed = true,
                                       .subj            = &cfg.key_train.val};
static button_item_t btn_key_tone   = make_encoder_btn(key_tone_label_getter, CTRL_KEY_TONE, &cfg.key_tone.val);

static button_item_t btn_key_mode        = make_encoder_btn(key_mode_label_getter, CTRL_KEY_MODE, &cfg.key_mode.val);
static button_item_t btn_key_iambic_mode = {.type            = BTN_TEXT_FN,
                                            .label_fn        = iambic_mode_label_getter,
                                            .press           = controls_toggle_key_iambic_mode,
                                            .hold            = button_encoder_hold_update_cb,
                                            .data            = CTRL_IAMBIC_MODE,
                                            .encoder_allowed = true,
                                            .subj            = &cfg.iambic_mode.val};
static button_item_t btn_key_qsk_time    = make_encoder_btn(qsk_time_label_getter, CTRL_QSK_TIME, &cfg.qsk_time.val);
static button_item_t btn_key_ratio       = make_encoder_btn(key_ratio_label_getter, CTRL_KEY_RATIO, &cfg.key_ratio.val);

static button_item_t btn_cw_decoder = {.type            = BTN_TEXT_FN,
                                       .label_fn        = cw_decoder_label_getter,
                                       .press           = controls_toggle_cw_decoder,
                                       .hold            = button_encoder_hold_update_cb,
                                       .data            = CTRL_CW_DECODER,
                                       .encoder_allowed = true,
                                       .subj            = &cfg.cw_decoder.val};
static button_item_t btn_cw_tuner   = {.type            = BTN_TEXT_FN,
                                       .label_fn        = cw_tuner_label_getter,
                                       .press           = controls_toggle_cw_tuner,
                                       .hold            = button_encoder_hold_update_cb,
                                       .data            = CTRL_CW_TUNE,
                                       .encoder_allowed = true,
                                       .subj            = &cfg.cw_tune.val};
static button_item_t btn_cw_snr = make_encoder_btn(cw_snr_label_getter, CTRL_CW_DECODER_SNR, &cfg.cw_decoder_snr.val);
static button_item_t btn_cw_peak_beta =
    make_encoder_btn(cw_peak_beta_label_getter, CTRL_CW_DECODER_PEAK_BETA, &cfg.cw_decoder_peak_beta.val);
static button_item_t btn_cw_noise_beta =
    make_encoder_btn(cw_noise_beta_label_getter, CTRL_CW_DECODER_NOISE_BETA, &cfg.cw_decoder_noise_beta.val);

/* DSP */

static button_item_t btn_dnf        = {.type            = BTN_TEXT_FN,
                                       .label_fn        = dnf_label_getter,
                                       .press           = controls_toggle_dnf,
                                       .hold            = button_encoder_hold_update_cb,
                                       .data            = CTRL_DNF,
                                       .encoder_allowed = true,
                                       .subj            = &cfg.dnf.val};
static button_item_t btn_dnf_center = make_encoder_btn(dnf_center_label_getter, CTRL_DNF_CENTER, &cfg.dnf_center.val);
static button_item_t btn_dnf_width  = make_encoder_btn(dnf_width_label_getter, CTRL_DNF_WIDTH, &cfg.dnf_width.val);
static button_item_t btn_dnf_auto   = {.type            = BTN_TEXT_FN,
                                       .label_fn        = dnf_auto_label_getter,
                                       .press           = controls_toggle_dnf_auto,
                                       .hold            = button_encoder_hold_update_cb,
                                       .data            = CTRL_DNF_AUTO,
                                       .encoder_allowed = true,
                                       .subj            = &cfg.dnf_auto.val};

static button_item_t btn_nb       = {.type            = BTN_TEXT_FN,
                                     .label_fn        = nb_label_getter,
                                     .press           = controls_toggle_nb,
                                     .hold            = button_encoder_hold_update_cb,
                                     .data            = CTRL_NB,
                                     .encoder_allowed = true,
                                     .subj            = &cfg.nb.val};
static button_item_t btn_nb_level = make_encoder_btn(nb_level_label_getter, CTRL_NB_LEVEL, &cfg.nb_level.val);
static button_item_t btn_nb_width = make_encoder_btn(nb_width_label_getter, CTRL_NB_WIDTH, &cfg.nb_width.val);

static button_item_t btn_nr       = {.type            = BTN_TEXT_FN,
                                     .label_fn        = nr_label_getter,
                                     .press           = controls_toggle_nr,
                                     .hold            = button_encoder_hold_update_cb,
                                     .data            = CTRL_NR,
                                     .encoder_allowed = true,
                                     .subj            = &cfg.nr.val};
static button_item_t btn_nr_level = make_encoder_btn(nr_level_label_getter, CTRL_NR_LEVEL, &cfg.nr_level.val);

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
    .press = button_encoder_update_cb,
    .data  = CTRL_RTTY_RATE,
    .encoder_allowed = true,
};
static button_item_t btn_rtty_shift = {
    .type  = BTN_TEXT,
    .label = "Shift",
    .press = button_encoder_update_cb,
    .data  = CTRL_RTTY_SHIFT,
    .encoder_allowed = true,
};
static button_item_t btn_rtty_center = {
    .type  = BTN_TEXT,
    .label = "Center",
    .press = button_encoder_update_cb,
    .data  = CTRL_RTTY_CENTER,
    .encoder_allowed = true,
};
static button_item_t btn_rtty_reverse = {
    .type  = BTN_TEXT,
    .label = "Reverse",
    .press = button_encoder_update_cb,
    .data  = CTRL_RTTY_REVERSE,
    .encoder_allowed = true,
};


/* VOL pages */
static button_item_t btn_vol_p1 = make_page_btn("(VOL 1:2)", "Volume|page 1");
static button_item_t btn_vol_p2 = make_page_btn("(VOL 2:2)", "Volume|page 2");

buttons_page_t buttons_page_vol_1 = {
    {&btn_vol_p1, &btn_vol, &btn_sql, &btn_rfg, &btn_tx_pwr}
};
static buttons_page_t page_vol_2 = {
    {&btn_vol_p2, &btn_mic_sel, &btn_hmic_gain, &btn_imic_hain, &btn_moni_lvl}
};

/* MFK pages */
static button_item_t btn_mfk_p1 = make_page_btn("(MFK 1:3)", "MFK|page 1");
static button_item_t btn_mfk_p2 = make_page_btn("(MFK 2:3)", "MFK|page 2");
static button_item_t btn_mfk_p3 = make_page_btn("(MFK 2:3)", "MFK|page 3");

static buttons_page_t page_mfk_1 = {
    {&btn_mfk_p1, &btn_rit, &btn_xit, &btn_zoom, &btn_ant}
};
static buttons_page_t page_mfk_2 = {
    {&btn_mfk_p2, &btn_agc_hang, &btn_agc_knee, &btn_agc_slope, &btn_comp}
};
static buttons_page_t page_mfk_3 = {
    {&btn_mfk_p3, &btn_if_shift}
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
    &page_mfk_1,
    &page_mfk_2,
    &page_mfk_3,
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

    if (x6100_control_get_base_ver().rev < 3) {
        // Hide DNF auto button
        page_dfn_1.items[4] = NULL;
    }

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

    /* Update default binds */
    binds.fill(ENCODER_BIND_MFK);
    for (auto it = std::begin(cfg_encoder_vol_modes_default); it != std::end(cfg_encoder_vol_modes_default); it++) {
        binds[*it] = ENCODER_BIND_VOL;
    }

    uint16_t y = 480 - BTN_HEIGHT;
    uint16_t x = 0;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_obj_create(parent);
        disp_btns[i].parent = f;

        lv_obj_remove_style_all(f);
        lv_obj_add_style(f, &btn_style, 0);
        lv_obj_add_style(f, &btn_active_style, LV_STATE_CHECKED);
        lv_obj_add_style(f, &btn_disabled_style, LV_STATE_DISABLED);
        // lv_obj_add_style(f, &btn_mark_assigned_style, STATE_ASSIGNED);

        lv_obj_set_pos(f, x, y);
        lv_obj_set_size(f, BTN_WIDTH, BTN_HEIGHT);
        x += BTN_WIDTH;

        /* Encoder marks */
        lv_obj_t *enc_mark;

        enc_mark = lv_obj_create(f);
        lv_obj_set_pos(enc_mark, 4, 4);

        lv_obj_set_style_bg_color(enc_mark, lv_color_hex(0xC0C0C0), 0);
        lv_obj_add_style(enc_mark, &btn_mark_style, 0);
        lv_obj_add_style(enc_mark, &btn_mark_assigned_style, STATE_ASSIGNED);

        disp_btns[i].vol_mark = enc_mark;


        enc_mark = lv_obj_create(f);
        lv_obj_set_pos(enc_mark, 4, BTN_HEIGHT - 4 - 10);

        lv_obj_set_style_bg_color(enc_mark, lv_color_hex(0x606060), 0);
        lv_obj_add_style(enc_mark, &btn_mark_style, 0);
        lv_obj_add_style(enc_mark, &btn_mark_assigned_style, STATE_ASSIGNED);
        disp_btns[i].mfk_mark = enc_mark;

        /* Label */
        lv_obj_t *label = lv_label_create(f);

        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        disp_btns[i].label = label;
    }

    subject_add_delayed_observer_and_call(cfg.encoders_binds.val, encoder_binds_change_cb, NULL);
}

void buttons_refresh(button_item_t *item) {
    if (item->disp_btn) {
        disp_btn_refresh(item->disp_btn);
    } else {
        LV_LOG_WARN("Button item label obj is null");
    }
}

void buttons_mark(button_item_t *item, bool val) {
    if (!item) {
        LV_LOG_INFO("Button item is null, skip mark");
        return;
    }
    item->mark = val;
    if (item->disp_btn) {
        disp_btn_refresh(item->disp_btn);
    }
}

void buttons_disabled(button_item_t *item, bool val) {
    item->disabled = val;
    if (item->disp_btn) {
        disp_btn_refresh(item->disp_btn);
    }
}

void buttons_load(uint8_t n, button_item_t *item) {
    button_item_t *prev_item = disp_btns[n].item;
    disp_btn_clear(disp_btns + n);

    lv_obj_t *label = disp_btns[n].label;
    disp_btns[n].item = item;
    if (item) {
        item->disp_btn = disp_btns + n;
        disp_btn_refresh(item->disp_btn);
    } else {
        lv_label_set_text(label, "");
    }
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
        disp_btn_clear(disp_btns + i);
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


static void button_encoder_update_cb(button_item_t *item) {
    // set corresponding encoder
    // if already bind - use corresponding encoder. If no - you default
    cfg_ctrl_t ctrl = (cfg_ctrl_t)item->data;
    char *binds = subject_get_text(cfg.encoders_binds.val);
    size_t n_binds = strlen((char *)binds);

    void (*set_fn)(cfg_ctrl_t) = NULL;
    if (n_binds > ctrl){
        switch (binds[ctrl]) {
            case ENCODER_BIND_VOL:
                set_fn = vol_set_ctrl;
                break;
            case ENCODER_BIND_MFK:
                set_fn = mfk_set_ctrl;
                break;
            default: ;
                auto it = std::find(std::begin(cfg_encoder_vol_modes_default), std::end(cfg_encoder_vol_modes_default), ctrl);
                if (it != std::end(cfg_encoder_vol_modes_default)) {
                    set_fn = vol_set_ctrl;
                } else {
                    it = std::find(std::begin(cfg_encoder_mfk_modes_default), std::end(cfg_encoder_mfk_modes_default), ctrl);
                    if (it != std::end(cfg_encoder_mfk_modes_default)) {
                        set_fn = mfk_set_ctrl;
                    }
                }
                break;
        }
    } else {
        set_fn = mfk_set_ctrl;
        // LV_LOG_ERROR("Binds is too short (%d) for ctrl %d", n_binds, ctrl);
    }
    if (set_fn) {
        set_fn(ctrl);
    }
    free(binds);
}

static void button_encoder_hold_update_cb(button_item_t *item) {
    cfg_ctrl_t ctrl = (cfg_ctrl_t)item->data;
    char *binds = (char *)subject_get_text(cfg.encoders_binds.val);
    size_t n_binds = strlen((char *)binds);
    if (n_binds > ctrl){
        switch (binds[ctrl]) {
            case ENCODER_BIND_NONE:
                msg_update_text_fmt("Added to VOL encoder");
                voice_say_text_fmt("Added to volume encoder");
                binds[ctrl] = ENCODER_BIND_VOL;
                break;
            case ENCODER_BIND_VOL:
                msg_update_text_fmt("Moved to MFK encoder");
                voice_say_text_fmt("Moved to MFK encoder");
                binds[ctrl] = ENCODER_BIND_MFK;
                break;
            case ENCODER_BIND_MFK:
                msg_update_text_fmt("Removed from MFK encoder");
                voice_say_text_fmt("Removed from MFK encoder");
                binds[ctrl] = ENCODER_BIND_NONE;
                break;
            default:
                binds[ctrl] = ENCODER_BIND_NONE;
                LV_LOG_WARN("Unexpected mode: %c for ctrl %d", binds[ctrl], ctrl);
                break;
        }
        subject_set_text(cfg.encoders_binds.val, (const char *)binds);
    } else {
        LV_LOG_ERROR("Binds is too short (%d) for ctrl %c", n_binds, ctrl);
    }
    free(binds);
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
    button_item_t *item = disp_btns[n].item;
    if (item == NULL) {
        LV_LOG_WARN("Button %u is NULL", n);
        return;
    }
    if (item->disabled) {
        LV_LOG_USER("Button %s disabled", lv_label_get_text(item->disp_btn->label));
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
    sprintf(buf, "MIC Sel:\n%s", params_mic_str_get((x6100_mic_sel_t)subject_get_int(cfg.mic.val)));
    return buf;
}


static const char * h_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "H-Mic gain:\n%zu", subject_get_int(cfg.hmic.val));
    return buf;
}

static const char * i_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "I-Mic gain:\n%zu", subject_get_int(cfg.imic.val));
    return buf;
}

static const char * moni_level_label_getter() {
    static char buf[22];
    sprintf(buf, "Moni level:\n%zu", subject_get_int(cfg.moni.val));
    return buf;
}


static const char * rit_label_getter() {
    static char buf[22];
    sprintf(buf, "RIT:\n%+zi", subject_get_int(cfg.rit.val));
    return buf;
}

static const char * xit_label_getter() {
    static char buf[22];
    sprintf(buf, "XIT:\n%+zi", subject_get_int(cfg.xit.val));
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

static const char * if_shift_label_getter() {
    static char buf[22];
    sprintf(buf, "IF shift:\n%d", subject_get_int(cfg_cur.band->if_shift.val));
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


static void encoder_binds_change_cb(Subject *subj, void *user_data) {
    char *binds = subject_get_text(cfg.encoders_binds.val);
    size_t n_binds = strlen((char *)binds);
    for (size_t i = 0; i < n_binds; i++) {
        fast_binds[i] = binds[i];
    }
    free(binds);

    for (size_t i = 0; i < BUTTONS; i++)
    {
        disp_btn_t *b = disp_btns + i;
        disp_btn_refresh(b);
    }
}

static void label_update_cb(Subject *subj, void *user_data) {
    button_item_t *item = (button_item_t*)user_data;
    if (item->disp_btn) {
        lv_label_set_text(item->disp_btn->label, item->label_fn());
    } else {
        LV_LOG_WARN("Can't update label: it's NULL");
    }
}


static void disp_btn_refresh(disp_btn_t *b) {
    button_item_t *bi = b->item;
    if (!bi) {
        LV_LOG_WARN("Button has no assigned data");
        return;
    }

    /* Label */

    if (bi->type == BTN_TEXT) {
        lv_label_set_text(b->label, bi->label);
    } else if (bi->type == BTN_TEXT_FN) {
        lv_label_set_text(b->label, bi->label_fn());
        if (bi->subj && *bi->subj) {
            bi->observer = (*bi->subj)->subscribe_delayed(label_update_cb, bi);
        } else {
            lv_obj_set_user_data(b->label, (void *)bi->label_fn);
        }
    } else {
        lv_label_set_text(b->label, "");
    }

    /* Marked button */

    if (bi->mark) {
        lv_obj_add_state(b->parent, LV_STATE_CHECKED);

    } else {
        lv_obj_clear_state(b->parent, LV_STATE_CHECKED);
    }

    /* Disabled button */

    if (bi->disabled) {
        lv_obj_add_state(b->parent, LV_STATE_DISABLED);

    } else {
        lv_obj_clear_state(b->parent, LV_STATE_DISABLED);
    }

    /* Encoder */

    if ((bi->data >= 0) && bi->encoder_allowed) {
        cfg_ctrl_t ctrl = (cfg_ctrl_t)bi->data;
        encoder_binds_t encoder      = ENCODER_BIND_NONE,
                        fast_encoder = ENCODER_BIND_NONE;

        if (ctrl < std::size(fast_binds)) {
            fast_encoder = (encoder_binds_t)fast_binds[ctrl];
        }
        if (fast_encoder != ENCODER_BIND_NONE) {
            encoder = fast_encoder;
            lv_obj_add_state(b->vol_mark, STATE_ASSIGNED);
            lv_obj_add_state(b->mfk_mark, STATE_ASSIGNED);
        } else {
            lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
            lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
            if ((ctrl < std::size(binds)) && (bi->press == button_encoder_update_cb)) {
                encoder = (encoder_binds_t)binds[ctrl];
            }
        }
        lv_obj_t *active_encoder_marker = NULL;
        switch (encoder) {
            case ENCODER_BIND_VOL:
                lv_obj_clear_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                break;
            case ENCODER_BIND_MFK:
                lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                break;
            default:
                lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
                lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
                break;
        }
    } else {
        lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
        lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
    }
}


static void disp_btn_clear(disp_btn_t *b) {
    lv_obj_t *label = b->label;

    lv_label_set_text(label, "");
    lv_obj_set_user_data(label, NULL);
    lv_obj_clear_state(b->parent, LV_STATE_CHECKED);
    lv_obj_clear_state(b->parent, LV_STATE_DISABLED);

    lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
    lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
    lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);

    if (b->item) {
        b->item->disp_btn = NULL;
        if (b->item->observer) {
            delete b->item->observer;
            b->item->observer = NULL;
        }
        b->item = NULL;
    }
}
