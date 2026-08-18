/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "info.h"

#include "cfg/cfg_api.h"
#include "styles.h"
#include "params/params.h"
#include "pubsub_ids.h"
#include "wifi.h"

typedef enum {
    INFO_VFO = 0,
    INFO_MODE,
    INFO_AGC,
    INFO_PRE,
    INFO_ATT,
    INFO_ATU,
    INFO_WIFI
} info_items_t;

static lv_obj_t     *obj;
static lv_obj_t     *items[7];

static SubjectInt *mode_lock;

static void wifi_state_change_cb(void *s, lv_msg_t *m);

static void vfo_label_update(Subject *subj, void * user_data);
static void mode_label_update(Subject *subj, void * user_data);
static void atu_label_update(Subject *subj, void * user_data);
static void agc_label_update(Subject *subj, void * user_data);
static void att_label_update(Subject *subj, void * user_data);
static void pre_label_update(Subject *subj, void * user_data);

lv_obj_t * info_init(lv_obj_t * parent) {

    mode_lock = subject_i_create(false);

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &info_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *row1 = lv_obj_create(obj);
    lv_obj_add_style(row1, &info_row_style, 0);
    lv_obj_set_size(row1, 190, 24);
    lv_obj_align(row1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);

    uint8_t i = 0;
    for (; i < 3; i++) {
        lv_obj_t *item = lv_label_create(row1);
        lv_obj_set_flex_grow(item, 1);
        items[i] = item;
    }

    lv_obj_t *row2 = lv_obj_create(obj);
    lv_obj_add_style(row2, &info_row_style, 0);
    lv_obj_set_size(row2, 190, 24);
    lv_obj_align(row2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);

    for (; i < sizeof(items) / sizeof(*items); i++) {
        lv_obj_t *item = lv_label_create(row2);
        lv_obj_set_flex_grow(item, 3);
        items[i] = item;
    }

    lv_obj_set_flex_grow(items[INFO_WIFI], 2);

    for (i = 0; i < sizeof(items) / sizeof(*items); i++)
    {
        lv_obj_add_style(items[i], &info_item_style, 0);
        lv_obj_set_style_text_align(items[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(items[i], lv_color_white(), 0);
    }

    lv_label_set_text(items[INFO_PRE], "PRE");
    lv_label_set_text(items[INFO_ATT], "ATT");
    lv_label_set_text(items[INFO_WIFI], LV_SYMBOL_WIFI " ");
    lv_obj_set_style_text_color(items[INFO_WIFI], lv_color_hex(0x909090), 0);

    subject_subscribe_delayed((Subject*)cfg_band_current_vfo, vfo_label_update, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_band_split, vfo_label_update, NULL);

    subject_subscribe_delayed((Subject*)cfg_cur_mode, mode_label_update, NULL);
    subject_subscribe_delayed_and_notify((Subject*)mode_lock, mode_label_update, NULL);

    subject_subscribe_delayed((Subject*)cfg_ant_id, atu_label_update, NULL);
    subject_subscribe_delayed((Subject*)cfg_fg_freq, atu_label_update, NULL);
    cfg_atu_loaded_subscribe_delayed(atu_label_update, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_atu_enabled, atu_label_update, NULL);

    subject_subscribe_delayed_and_notify((Subject*)cfg_cur_agc, agc_label_update, NULL);

    subject_subscribe_delayed_and_notify((Subject*)cfg_cur_att, att_label_update, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_cur_pre, pre_label_update, NULL);

    lv_msg_subscribe(MSG_WIFI_STATE_CHANGED, wifi_state_change_cb, NULL);

    return obj;
}

const char* info_params_mode_label_get() {
    x6100_mode_t    mode = cparam_i_get(cfg_cur_mode);
    char            *str;

    switch (mode) {
        case x6100_mode_lsb:
            str = "LSB";
            break;

        case x6100_mode_lsb_dig:
            str = "LSB-D";
            break;

        case x6100_mode_usb:
            str = "USB";
            break;

        case x6100_mode_usb_dig:
            str = "USB-D";
            break;

        case x6100_mode_cw:
            str = "CW";
            break;

        case x6100_mode_cwr:
            str = "CW-R";
            break;

        case x6100_mode_am:
            str = "AM";
            break;

        case x6100_mode_nfm:
            str = "NFM";
            break;

        default:
            str = "?";
            break;
    }

    return str;
}

const char* info_params_agc() {
    x6100_agc_t     agc = cparam_i_get(cfg_cur_agc);
    char            *str;

    switch (agc) {
        case x6100_agc_off:
            str = "OFF";
            break;

        case x6100_agc_slow:
            str = "SLOW";
            break;

        case x6100_agc_fast:
            str = "FAST";
            break;

        case x6100_agc_auto:
            str = "AUTO";
            break;

        default:
            str = "?";
            break;

    }

    return str;
}

const char* info_params_vfo_label_get() {
    x6100_vfo_t cur_vfo = param_i_get(cfg_band_current_vfo);
    char            *str;

    if (param_i_get(cfg_band_split)) {
        str = cur_vfo == X6100_VFO_A ? "SPL-A" : "SPL-B";
    } else {
        str = cur_vfo == X6100_VFO_A ? "VFO-A" : "VFO-B";
    }

    return str;
}

void info_lock_mode(bool lock) {
    subject_i_set(mode_lock, lock);
}

static void wifi_state_change_cb(void *s, lv_msg_t *m) {
    lv_color_t color;
    switch (wifi_get_status()) {
    case WIFI_CONNECTED:
        color = lv_color_white();
        break;
    case WIFI_OFF:
        color = lv_color_black();
        break;
    default:
        color = lv_color_hex(0x909090);
        break;
    }
    lv_obj_set_style_text_color(items[INFO_WIFI], color, 0);
}


static void vfo_label_update(Subject *subj, void * user_data) {
    lv_label_set_text(items[INFO_VFO], info_params_vfo_label_get());
}

static void mode_label_update(Subject *subj, void *user_data) {
    lv_label_set_text(items[INFO_MODE], info_params_mode_label_get());
    x6100_mode_t mode = cparam_i_get(cfg_cur_mode);
    if ((mode == x6100_mode_lsb_dig) || (mode == x6100_mode_usb_dig)) {
        lv_obj_set_style_text_color(items[INFO_MODE], lv_color_hex(COLOR_LIGHT_RED), 0);
    } else if (subject_i_get(mode_lock)) {
        lv_obj_set_style_text_color(items[INFO_MODE], lv_color_hex(0xAAAAAA), 0);
    } else {
        lv_obj_set_style_text_color(items[INFO_MODE], lv_color_white(), 0);
    }
}

static void atu_label_update(Subject *subj, void * user_data) {
    int32_t ant = param_i_get(cfg_ant_id);
    lv_label_set_text_fmt(items[INFO_ATU], "ATU%i", ant);
    int32_t freq = cparam_i_get(cfg_fg_freq);

    if (!param_i_get(cfg_atu_enabled)) {
        lv_obj_set_style_text_color(items[INFO_ATU], lv_color_white(), 0);
        lv_obj_set_style_bg_color(items[INFO_ATU], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(items[INFO_ATU], LV_OPA_0, 0);
    } else {
        if (cfg_transverter_shift_for(freq)) {
            lv_obj_set_style_text_color(items[INFO_ATU], lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_bg_opa(items[INFO_ATU], LV_OPA_20, 0);
        } else {
            lv_obj_set_style_text_color(items[INFO_ATU], cfg_atu_is_loaded() ? lv_color_black() : lv_color_hex(0xFF0000), 0);
            lv_obj_set_style_bg_opa(items[INFO_ATU], LV_OPA_50, 0);
        }
        lv_obj_set_style_bg_color(items[INFO_ATU], lv_color_white(), 0);
    }
}

static void agc_label_update(Subject *subj, void * user_data) {
    lv_label_set_text(items[INFO_AGC], info_params_agc());
}

static void att_label_update(Subject *subj, void * user_data) {
    if (cparam_i_get(cfg_cur_att)) {
        lv_obj_set_style_text_color(items[INFO_ATT], lv_color_black(), 0);
        lv_obj_set_style_bg_color(items[INFO_ATT], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(items[INFO_ATT], LV_OPA_50, 0);
    } else {
        lv_obj_set_style_text_color(items[INFO_ATT], lv_color_white(), 0);
        lv_obj_set_style_bg_color(items[INFO_ATT], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(items[INFO_ATT], LV_OPA_0, 0);
    }
}

static void pre_label_update(Subject *subj, void * user_data) {
    if (cparam_i_get(cfg_cur_pre)) {
        lv_obj_set_style_text_color(items[INFO_PRE], lv_color_black(), 0);
        lv_obj_set_style_bg_color(items[INFO_PRE], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(items[INFO_PRE], LV_OPA_50, 0);
    } else {
        lv_obj_set_style_text_color(items[INFO_PRE], lv_color_white(), 0);
        lv_obj_set_style_bg_color(items[INFO_PRE], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(items[INFO_PRE], LV_OPA_0, 0);
    }
}
