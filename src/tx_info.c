/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "tx_info.h"

#include <stdio.h>

#include "events.h"
#include "msg_tiny.h"
#include "cfg/cfg_api.h"
#include "scheduler.h"
#include "styles.h"
#include "util.h"
#include "widgets/lv_bar_indicator.h"

#define UPDATE_UI_MS 40

static const float min_pwr = 0.0f;
static const float max_pwr = 10.0f;

static const float min_swr = 1.0f;
static const float max_swr = 5.0f;

static float pwr  = 0.0f;
static float vswr = 0.0f;
static float alc;

static uint8_t msg_id;

static uint64_t prev_ui_update = 0;

static x6100_mode_t cur_mode;

static bool dialog_run = false;

static lv_obj_t     *obj;
static lv_obj_t     *pwr_label;
static lv_obj_t     *vswr_label;
static lv_obj_t     *alc_label;
static lv_obj_t     *pwr_bar;
static lv_obj_t     *swr_bar;

static swr_color_t last_swr_color = -1;

static bar_tick_t pwr_ticks[] = {
    {.label = "PWR", .val = 0.0f },
    {.label = "2",   .val = 2.0f },
    {.label = "4",   .val = 4.0f },
    {.label = "6",   .val = 6.0f },
    {.label = "8",   .val = 8.0f },
    {.label = "10",  .val = 10.0f}
};

static bar_tick_t vswr_ticks[] = {
    {.label = "SWR", .val = 1.0f},
    {.label = "2",   .val = 2.0f},
    {.label = "3",   .val = 3.0f},
    {.label = "4",   .val = 4.0f},
    {.label = ">5",  .val = 5.0f}
};

static void on_cur_mode_change(Subject *subj, void *user_data);
static void update_labels_visibility_cb(Subject *, void *);

static void tx_cb(void * s, lv_msg_t * msg) {
    pwr  = 0.0f;
    vswr = 0.0f;
    alc  = 0.0f;

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void rx_cb(void * s, lv_msg_t * msg) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void update_tx_info(void *arg) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_bar_indicator_set_value(pwr_bar, pwr);
    lv_bar_indicator_set_value(swr_bar, vswr);

    if (param_i_get(cfg.ui.mag_alc()) && !dialog_run) {
        msg_tiny_set_text_fmt("ALC: %.1f", alc);
    }
}

static void update_labels_cb(lv_timer_t *t) {
    lv_label_set_text_fmt(alc_label, "ALC: %1.1f", alc);
    lv_label_set_text_fmt(vswr_label, "%.2f", vswr);
    lv_label_set_text_fmt(pwr_label, "%.2f", pwr);
}

static void on_dialog_start(void *s, lv_msg_t *msg) {
    dialog_run = true;
    update_labels_visibility_cb(NULL, NULL);
}

static void on_dialog_stop(void *s, lv_msg_t *msg) {
    dialog_run = false;
    update_labels_visibility_cb(NULL, NULL);
}

static lv_color_t swr_bar_color_cb(float val) {
    if (val <= 2.0f) {
        return param_i_get(cfg.ui.swr_color()) == SWR_GRAY ? lv_color_hex(0xAAAAAA) : lv_color_hex(0x00CC00);
    } else if (val <= 3.0f) {
        return lv_color_hex(0xAAAA00);
    } else {
        return lv_color_hex(0xAA0000);
    }
}

lv_obj_t *tx_info_init(lv_obj_t *parent) {
    obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &style.tx_info, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

    // Use pad to align
    lv_coord_t pad = lv_obj_get_style_pad_top(obj, 0);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(obj);
    lv_coord_t w = lv_obj_get_content_width(obj);
    lv_coord_t h = lv_obj_get_content_height(obj);

    /* PWR indicator */
    pwr_bar = lv_bar_indicator_create(obj);
    lv_obj_set_size(pwr_bar, w, (h - pad) / 2);
    lv_bar_indicator_set_range(pwr_bar, min_pwr, max_pwr, 0.25f);
    lv_bar_indicator_set_ticks(pwr_bar, pwr_ticks, ARRAY_SIZE(pwr_ticks));
    lv_bar_indicator_set_default_color(pwr_bar, lv_color_hex(0xAAAAAA));

    lv_bar_indicator_set_font(pwr_bar, &sony_22);

    /* SWR indicator */
    swr_bar = lv_bar_indicator_create(obj);
    lv_obj_set_size(swr_bar, w, (h - pad) / 2);
    lv_bar_indicator_set_range(swr_bar, min_swr, max_swr, 0.1f);
    lv_bar_indicator_set_ticks(swr_bar, vswr_ticks, ARRAY_SIZE(vswr_ticks));

    lv_bar_indicator_set_font(swr_bar, &sony_22);
    lv_obj_set_y(swr_bar, (h + pad) / 2);

    lv_bar_indicator_set_default_color(swr_bar, lv_color_hex(0xAA0000));
    lv_bar_indicator_set_color_cb(swr_bar, swr_bar_color_cb);

    // Small alc label
    alc_label = lv_label_create(obj);
    lv_obj_add_style(alc_label, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(alc_label, &sony_20, 0);
    lv_obj_align(alc_label, LV_ALIGN_BOTTOM_RIGHT, pad - 3, pad - 2);
    lv_label_set_text(alc_label, "");

    // pwr label
    pwr_label = lv_label_create(obj);
    lv_obj_add_style(pwr_label, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(pwr_label, &sony_20, 0);
    lv_obj_align(pwr_label, LV_ALIGN_BOTTOM_RIGHT, pad - 3, -h / 2 - 2);
    lv_label_set_text(pwr_label, "");

    // swr label
    vswr_label = lv_label_create(obj);
    lv_obj_add_style(vswr_label, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(vswr_label, &sony_20, 0);
    lv_obj_align(vswr_label, LV_ALIGN_BOTTOM_RIGHT, pad - 3, pad - 2);
    lv_label_set_text(vswr_label, "");

    lv_msg_subscribe(MSG_RADIO_TX, tx_cb, NULL);
    lv_msg_subscribe(MSG_RADIO_RX, rx_cb, NULL);
    lv_msg_subscribe(MSG_DIALOG_START, on_dialog_start, NULL);
    lv_msg_subscribe(MSG_DIALOG_STOP, on_dialog_stop, NULL);

    subject_subscribe((Subject*)cfg.cur.mode(), on_cur_mode_change, NULL);

    subject_subscribe_delayed((Subject*)cfg.ui.mag_alc(), update_labels_visibility_cb, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg.ui.show_meter_value(), update_labels_visibility_cb, NULL);

    lv_timer_create(update_labels_cb, LV_DISP_DEF_REFR_PERIOD * 3, NULL);

    return obj;
}

void tx_info_update(float p, float s, float a) {
    const float beta = 0.9f;

    a = 10.f - a;
    s = LV_MIN(max_swr, s);

    switch (cur_mode) {
        case x6100_mode_lsb_dig:
        case x6100_mode_usb_dig:
            pwr  = p;
            alc  = a;
            vswr = s;
            break;
        default:
            lpf(&pwr, p, beta, 0.0f);
            lpf(&alc, a, beta, 0.0f);
            lpf(&vswr, s, beta, 0.0f);
    }
    msg_id++;
    scheduler_put_noargs(update_tx_info);
}

bool tx_info_refresh(uint8_t *prev_msg_id, float *alc_p, float *pwr_p, float *vswr_p) {
    if (*prev_msg_id == msg_id) {
        return false;
    }
    if (alc_p)
        *alc_p = alc;
    if (pwr_p)
        *pwr_p = pwr;
    if (vswr_p)
        *vswr_p = vswr;
    *prev_msg_id = msg_id;
    return true;
}


static void on_cur_mode_change(Subject *subj, void *user_data) {
    cur_mode = cparam_i_get(cfg.cur.mode());
}

static void update_labels_visibility_cb(Subject *, void *) {
    bool small_alc_required = !param_i_get(cfg.ui.mag_alc()) || dialog_run;
    if (small_alc_required) {
        lv_obj_clear_flag(alc_label, LV_OBJ_FLAG_HIDDEN);
        // Hide VSWR
        lv_obj_add_flag(vswr_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(alc_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (param_i_get(cfg.ui.show_meter_value())) {
        lv_obj_clear_flag(pwr_label, LV_OBJ_FLAG_HIDDEN);
        if (!small_alc_required) {
            lv_obj_clear_flag(vswr_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(pwr_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(vswr_label, LV_OBJ_FLAG_HIDDEN);
    }
}
