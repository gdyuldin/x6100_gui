/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "meter.h"
#include "styles.h"
#include "events.h"
#include "params/params.h"
#include "cfg/cfg_api.h"
#include "spectrum.h"
#include "util.h"
#include "scheduler.h"
#include "widgets/lv_bar_indicator.h"

#define NUM_ITEMS   7
#define METER_PEAK_HOLD 1500
#define METER_PEAK_SPEED 20

static int16_t          min_db = S1;
static int16_t          max_db = S9_40;

static float            meter_db = S1;
static float            meter_db_raw = S1;
static float            noise_level = S_MIN;

static float            meter_peak = S1;
static int64_t          meter_peak_time;
static int64_t          now;

static bool             pre=false;
static bool             att=false;

static lv_obj_t         *obj;
static lv_obj_t         *bar;
static lv_obj_t         *db_val_label;

static bar_tick_t s_items[NUM_ITEMS] = {
    { .label = "S1",    .val = S1 },
    { .label = "3",     .val = S3 },
    { .label = "5",     .val = S5 },
    { .label = "7",     .val = S7 },
    { .label = "9",     .val = S9 },
    { .label = "+20",   .val = S9_20 },
    { .label = "+40",   .val = S9_40 }
};

static void on_bool_value_change(Subject *subj, void *user_data) {
    *(bool*)user_data = subject_i_get((SubjectInt*)subj);
}

static void meter_scheduled_refresh(void *unused) {
    (void)unused;
    lv_bar_indicator_set_value(bar, meter_db);
    lv_bar_indicator_set_peak_value(bar, meter_peak);
}

static void update_db_label_cb(lv_timer_t *t) {
    // TODO: add check for visibility
    lv_label_set_text_fmt(db_val_label, "%.1f", meter_db_raw);
}

static void tx_cb(void * s, lv_msg_t * msg) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void rx_cb(void * s, lv_msg_t * msg) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_color_t meter_color_cb(float val) {
    if (val <= noise_level) {
        return meter_color_noise;
    } else if (val <= S9) {
        return meter_color_s9;
    } else if (val <= S9_20) {
        return meter_color_s9plus;
    }
    return meter_color_over;
}


lv_obj_t * meter_init(lv_obj_t * parent) {
    obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &style.meter, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

    // Use pad to align
    lv_coord_t pad = lv_obj_get_style_pad_top(obj, 0);
    lv_obj_update_layout(obj);
    lv_coord_t w = lv_obj_get_content_width(obj);
    lv_coord_t h = lv_obj_get_content_height(obj);

    lv_msg_subscribe(MSG_RADIO_TX, tx_cb, NULL);
    lv_msg_subscribe(MSG_RADIO_RX, rx_cb, NULL);

    bar = lv_bar_indicator_create(obj);
    lv_obj_set_size(bar, w, h);
    lv_obj_center(bar);

    lv_bar_indicator_set_range(bar, S1, S9_40 + 5, 3.0f);

    lv_bar_indicator_set_ticks(bar, s_items, NUM_ITEMS);
    lv_bar_indicator_set_font(bar, &sony_22);
    lv_bar_indicator_set_default_color(bar, meter_color_s9);
    lv_bar_indicator_set_color_cb(bar, meter_color_cb);

    lv_bar_indicator_set_peak_enable(bar, true);
    lv_bar_indicator_set_peak_color(bar, meter_color_peak);

    subject_subscribe_delayed_and_notify((Subject*)cfg_cur_pre, on_bool_value_change, &pre);
    subject_subscribe_delayed_and_notify((Subject*)cfg_cur_att, on_bool_value_change, &att);

    db_val_label = lv_label_create(obj);
    lv_obj_set_style_text_font(db_val_label, &sony_20, 0);
    lv_obj_align(db_val_label, LV_ALIGN_BOTTOM_RIGHT, pad - 3, pad - 2);
    lv_obj_set_style_text_color(db_val_label, lv_color_white(), 0);
    lv_label_set_text(db_val_label, "");

    lv_timer_create(update_db_label_cb, LV_DISP_DEF_REFR_PERIOD * 3, NULL);

    return obj;
}

void meter_set_noise(float val) {
    noise_level = val;
    if (att) {
        noise_level+= 14.0f;
    }
    if (pre){
        noise_level -= 14.0f;
    }
}

void meter_update(float db, float beta) {
    if (att) {
        db += 15.0f;
    }
    if (pre){
        db -= 19.0f;
    }
    if (db < min_db) {
        db = min_db;
    } else if (db > max_db) {
        db = max_db;
    }
    meter_db_raw = db;
    now = get_time();
    if (db > meter_peak) {
        meter_peak = db;
        meter_peak_time = now;
    } else if (now - meter_peak_time > METER_PEAK_HOLD) {
        meter_peak -= (now - meter_peak_time - METER_PEAK_HOLD) * METER_PEAK_SPEED / 1000;
    }
    meter_db = meter_db * beta + db * (1.0f - beta);
    scheduler_put_noargs(meter_scheduled_refresh);
}

int16_t meter_get_raw_db() {
    return meter_db_raw;
}
