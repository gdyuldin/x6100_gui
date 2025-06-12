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
#include "spectrum.h"
#include "util.h"

#define NUM_ITEMS   7
#define METER_PEAK_HOLD 1500
#define METER_PEAK_SPEED 20

static int16_t          min_db = S1;
static int16_t          max_db = S9_40;

static int16_t          meter_db = S1;
static int16_t          meter_db_raw = S1;
static float            noise_level = S_MIN;

static int16_t          meter_peak = S1;
static int64_t          meter_peak_time;
static int64_t          now;

static bool             pre=false;
static bool             att=false;

static lv_obj_t         *obj;

typedef struct {
    char    *label;
    int16_t db;
} s_item_t;

static s_item_t s_items[NUM_ITEMS] = {
    { .label = "S1",    .db = S1 },
    { .label = "3",     .db = S3 },
    { .label = "5",     .db = S5 },
    { .label = "7",     .db = S7 },
    { .label = "9",     .db = S9 },
    { .label = "+20",   .db = S9_20 },
    { .label = "+40",   .db = S9_40 }
};

static void on_bool_value_change(Subject *subj, void *user_data) {
    *(bool*)user_data = subject_get_int(subj);
}

static void meter_draw_cb(lv_event_t * e) {
    lv_obj_t            *obj = lv_event_get_target(e);
    lv_draw_ctx_t       *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t  rect_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t           area;

    lv_coord_t x1 = obj->coords.x1 + 7;
    lv_coord_t y1 = obj->coords.y1 + 17;

    lv_coord_t w = lv_obj_get_width(obj) - 80;
    // lv_coord_t h = lv_obj_get_height(obj) - 1;

    uint8_t     slice_db = 3;
    uint8_t     slices_total = (max_db - min_db) / slice_db;
    uint8_t     slice_w = w / slices_total;
    uint8_t     slice_spacing = slice_w * 2 / 10;

    /* Rects */

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_opa = LV_OPA_80;

    uint32_t count = (meter_db - min_db + slice_db) / slice_db;
    count = LV_MIN(count, slices_total);

    area.y1 = y1 - 5;
    area.y2 = y1 + 32;

    int16_t db = s_items[0].db;

    for (uint16_t i = 0; i < count; i++) {
        if (db <= noise_level) {
            rect_dsc.bg_color = lv_color_hex(0x00CC00);
        } else if (db <= -73) {
            rect_dsc.bg_color = lv_color_hex(0x00CC00);
        } else if (db <= -53) {
            rect_dsc.bg_color = lv_color_hex(0xFFFF00);
        } else {
            rect_dsc.bg_color = lv_color_hex(0xAA0000);
        }
        area.x1 = x1 + 30 + i * slice_w - slice_w / 2 + slice_spacing / 2;
        area.x2 = area.x1 + slice_w - slice_spacing;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);

        db += slice_db;
    }

    /* Peak */
    if (meter_peak > meter_db) {
        area.x1 = x1 + 30  - slice_w / 2 + slice_w * ((meter_peak - min_db) / slice_db);
        area.x2 = area.x1 + slice_w - slice_spacing;
        rect_dsc.bg_opa = LV_OPA_50;
        rect_dsc.bg_color = lv_color_hex(0xFFFF00);
        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    /* Labels */

    lv_draw_label_dsc_init(&label_dsc);

    label_dsc.color = lv_color_white();
    label_dsc.font = &sony_22;

    area.x1 = x1;
    area.x2 = x1 + 20;
    area.y1 = y1 + 5;
    area.y2 = area.y1 + 18;

    lv_point_t label_size;

    for (uint8_t i = 0; i < NUM_ITEMS; i++) {
        char    *label = s_items[i].label;
        int16_t db = s_items[i].db;

        lv_txt_get_size(&label_size, label, label_dsc.font, 0, 0, LV_COORD_MAX, 0);

        area.x1 = x1 + 30 + slice_w * ((db  - min_db) / slice_db) - label_size.x / 2;
        area.x2 = area.x1 + label_size.x;

        lv_draw_label(draw_ctx, &label_dsc, &area, label, NULL);
    }
}

static void tx_cb(lv_event_t * e) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void rx_cb(lv_event_t * e) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}


lv_obj_t * meter_init(lv_obj_t * parent) {
    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &meter_style, 0);

    lv_obj_add_event_cb(obj, tx_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, rx_cb, EVENT_RADIO_RX, NULL);
    lv_obj_add_event_cb(obj, meter_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    subject_add_delayed_observer(cfg_cur.pre, on_bool_value_change, &pre);
    on_bool_value_change(cfg_cur.pre, &pre);
    subject_add_delayed_observer(cfg_cur.att, on_bool_value_change, &att);
    on_bool_value_change(cfg_cur.att, &att);

    return obj;
}

void meter_update(int16_t db, float beta) {
    noise_level = spectrum_get_min();
    if (att) {
        db += 14;
        noise_level+= 14.0f;
    }
    if (pre){
        db -= 14;
        noise_level -= 14.0f;
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
    event_send(obj, LV_EVENT_REFRESH, NULL);
}

int16_t meter_get_raw_db() {
    return meter_db_raw;
}
