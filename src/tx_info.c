/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "tx_info.h"

#include <stdio.h>

#include "dialog.h"
#include "events.h"
#include "msg_tiny.h"
#include "params/params.h"
#include "scheduler.h"
#include "styles.h"
#include "util.h"

#define NUM_PWR_ITEMS 6
#define NUM_ALC_ITEMS 6
#define NUM_VSWR_ITEMS 5
#define UPDATE_UI_MS 40

static const float min_pwr = 0.0f;
static const float max_pwr = 10.0f;

static const float min_swr = 1.0f;
static const float max_swr = 5.0f;

static const float min_alc = 0.0f;
static const float max_alc = 10.0f;

static float pwr  = 0.0f;
static float vswr = 0.0f;
static float alc;

static uint8_t msg_id;

static uint64_t prev_ui_update = 0;

static x6100_mode_t cur_mode;

static lv_obj_t     *obj;
static lv_obj_t     *alc_label;
static lv_grad_dsc_t grad;

typedef struct {
    char *label;
    float val;
} item_t;

static item_t pwr_items[NUM_PWR_ITEMS] = {
    {.label = "PWR", .val = 0.0f },
    {.label = "2",   .val = 2.0f },
    {.label = "4",   .val = 4.0f },
    {.label = "6",   .val = 6.0f },
    {.label = "8",   .val = 8.0f },
    {.label = "10",  .val = 10.0f}
};

static item_t alc_items[NUM_ALC_ITEMS] = {
    {.label = "ALC", .val = 0.0f },
    {.label = "2",   .val = 2.0f },
    {.label = "4",   .val = 4.0f },
    {.label = "6",   .val = 6.0f },
    {.label = "8",   .val = 8.0f },
    {.label = "10",  .val = 10.0f}
};

static item_t vswr_items[NUM_VSWR_ITEMS] = {
    {.label = "SWR", .val = 1.0f},
    {.label = "2",   .val = 2.0f},
    {.label = "3",   .val = 3.0f},
    {.label = "4",   .val = 4.0f},
    {.label = ">5",  .val = 5.0f}
};

static void on_cur_mode_change(Subject *subj, void *user_data);

static void tx_info_draw_cb(lv_event_t *e) {
    lv_obj_t           *obj      = lv_event_get_target(e);
    lv_draw_ctx_t      *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t  rect_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t           area;
    lv_point_t          label_size;
    uint32_t            count;
    uint8_t             slices_total;
    uint8_t             slice_spacing = 2;

    lv_coord_t x1 = obj->coords.x1 + 7;
    lv_coord_t y1 = obj->coords.y1 + 17;

    lv_coord_t w = lv_obj_get_width(obj) - 60;
    lv_coord_t h = lv_obj_get_height(obj) - 1;

    /* PWR rects */

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_opa = LV_OPA_80;

    float slice_pwr_step    = 0.25f;
    slices_total            = (max_pwr - min_pwr) / slice_pwr_step;
    uint8_t slice_pwr_width = w / slices_total;

    count = (pwr - min_pwr + slice_pwr_step) / slice_pwr_step;
    count = LV_MIN(count, slices_total);

    area.y1 = y1 - 5;
    area.y2 = y1 + 32;

    rect_dsc.bg_color = lv_color_hex(0xAAAAAA);

    for (uint16_t i = 0; i < count; i++) {

        area.x1 = x1 + 30 + i * slice_pwr_width - slice_pwr_width / 2 + slice_spacing / 2;
        area.x2 = area.x1 + slice_pwr_width - slice_spacing;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    /* SWR rects */

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_opa = LV_OPA_80;

    float slice_swr_step    = 0.1f;
    slices_total            = (max_swr - min_swr) / slice_swr_step;
    uint8_t slice_swr_width = w / slices_total;

    count = (vswr - min_swr + slice_swr_step) / slice_swr_step;

    area.y1 = y1 - 5 + 54;
    area.y2 = y1 + 32 + 54;

    float swr_val = vswr_items[0].val;

    for (uint16_t i = 0; i < count; i++) {
        if (swr_val <= 2.0f) {
            rect_dsc.bg_color = lv_color_hex(0xAAAAAA);
        } else if (swr_val <= 3.0f) {
            rect_dsc.bg_color = lv_color_hex(0xAAAA00);
        } else {
            rect_dsc.bg_color = lv_color_hex(0xAA0000);
        }

        area.x1 = x1 + 30 + i * slice_swr_width - slice_swr_width / 2 + slice_spacing / 2;
        area.x2 = area.x1 + slice_swr_width - slice_spacing;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
        swr_val += slice_swr_step;
    }

    /* ALC rects */

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_opa = LV_OPA_80;

    float slice_alc_step    = 0.25f;
    slices_total            = (max_alc - min_alc) / slice_alc_step;
    uint8_t slice_alc_width = w / slices_total;

    count = (alc - min_alc + slice_alc_step) / slice_alc_step;
    count = LV_MIN(count, slices_total);

    area.y1 = y1 - 5 + 108;
    area.y2 = y1 + 32 + 108;

    rect_dsc.bg_color = lv_color_hex(0xAAAAAA);

    for (uint16_t i = 0; i < count; i++) {

        area.x1 = x1 + 30 + i * slice_alc_width - slice_alc_width / 2 + slice_spacing / 2;
        area.x2 = area.x1 + slice_alc_width - slice_spacing;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    /* PWR Labels */

    lv_draw_label_dsc_init(&label_dsc);

    label_dsc.color = lv_color_white();
    label_dsc.font  = &sony_22;

    area.x1 = x1;
    area.x2 = x1 + 20;

    area.y1 = y1 + 5;
    area.y2 = area.y1 + 18;

    for (uint8_t i = 0; i < NUM_PWR_ITEMS; i++) {
        char *label = pwr_items[i].label;
        float val   = pwr_items[i].val;

        lv_txt_get_size(&label_size, label, label_dsc.font, 0, 0, LV_COORD_MAX, 0);

        area.x1 = x1 + 30 + slice_pwr_width * ((val - min_pwr) / slice_pwr_step) - label_size.x / 2;
        area.x2 = area.x1 + label_size.x;

        lv_draw_label(draw_ctx, &label_dsc, &area, label, NULL);
    }

    /* SWR Labels */

    area.x1 = x1;
    area.x2 = x1 + 20;

    area.y1 = y1 + 60;
    area.y2 = y1 + 32 + 60;

    for (uint8_t i = 0; i < NUM_VSWR_ITEMS; i++) {
        char *label = vswr_items[i].label;
        float val   = vswr_items[i].val;

        lv_txt_get_size(&label_size, label, label_dsc.font, 0, 0, LV_COORD_MAX, 0);

        area.x1 = x1 + 30 + slice_swr_width * ((val - min_swr) / slice_swr_step) - label_size.x / 2;
        area.x2 = area.x1 + label_size.x;

        lv_draw_label(draw_ctx, &label_dsc, &area, label, NULL);
    }
    
    /* ALC Labels */

    area.x1 = x1;
    area.x2 = x1 + 20;

    area.y1 = y1 + 120;
    area.y2 = area.y1 + 32 + 120;

    for (uint8_t i = 0; i < NUM_ALC_ITEMS; i++) {
        char *label = alc_items[i].label;
        float val   = alc_items[i].val;

        lv_txt_get_size(&label_size, label, label_dsc.font, 0, 0, LV_COORD_MAX, 0);

        area.x1 = x1 + 30 + slice_alc_width * ((val - min_alc) / slice_alc_step) - label_size.x / 2;
        area.x2 = area.x1 + label_size.x;

        lv_draw_label(draw_ctx, &label_dsc, &area, label, NULL);
    }

}

static void tx_cb(lv_event_t *e) {
    pwr  = 0.0f;
    vswr = 0.0f;
    alc  = 0.0f;

    /* if the user has selected ALC MAG then tweak the bar area height as appropriate */
    if (params.mag_alc.x) {
        lv_style_set_height(&tx_info_style, 123); /* was 123 */
        lv_obj_clear_flag(alc_label, LV_OBJ_FLAG_HIDDEN);
    } else {
    /* otherwise, show the bar and hide the small label */
        lv_style_set_height(&tx_info_style, 185); /* was 123 */
        lv_obj_add_flag(alc_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(obj);
}

static void rx_cb(lv_event_t *e) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void update_tx_info(void *arg) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_obj_invalidate(obj);
    if (params.mag_alc.x) {
        msg_tiny_set_text_fmt("ALC: %.1f", alc); /* maybe hide this while we work on the bar graph? */
    }
    if (dialog_is_run() || !params.mag_alc.x) {
        lv_label_set_text_fmt(alc_label, "ALC: %1.1f", alc);
    } else {
        lv_label_set_text(alc_label, "");
    }
}

lv_obj_t *tx_info_init(lv_obj_t *parent) {
    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &tx_info_style, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(obj, tx_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, rx_cb, EVENT_RADIO_RX, NULL);
    lv_obj_add_event_cb(obj, tx_info_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    grad.dir         = LV_GRAD_DIR_VER;
    grad.stops_count = 4;

    grad.stops[0].color = lv_color_lighten(bg_color, 200);
    grad.stops[1].color = bg_color;
    grad.stops[2].color = bg_color;
    grad.stops[3].color = lv_color_darken(bg_color, 200);

    grad.stops[0].frac = 0;
    grad.stops[1].frac = 128 - 10;
    grad.stops[2].frac = 128 + 10;
    grad.stops[3].frac = 255;

    // Small alc indicator
    alc_label = lv_label_create(obj);
    lv_obj_set_style_text_font(alc_label, &sony_20, 0);
    lv_obj_align(alc_label, LV_ALIGN_BOTTOM_RIGHT, -10, 13);
    lv_obj_set_style_text_color(alc_label, lv_color_white(), 0);
    lv_label_set_text(alc_label, "");

    subject_add_observer(cfg_cur.mode, on_cur_mode_change, NULL);

    return obj;
}

void tx_info_update(float p, float s, float a) {
    // Use EMA for smoothing values
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
    cur_mode = subject_get_int(subj);
}
