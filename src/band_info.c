/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "band_info.h"

#include "backlight.h"
#include "events.h"
#include "pubsub_ids.h"
#include "styles.h"

#include <stdlib.h>

#define BANDS_REFRESH_PERIOD_MS 3 * 1000

static lv_obj_t *obj;

static lv_coord_t   band_info_height = 24;
static int32_t      width_hz         = 100000;
static band_info_t *band_info;
static int32_t     band_info_cap    = 32;
static uint16_t     bands_count      = 0;
static uint64_t     freq;
static lv_anim_t    fade;
static bool         fade_run = false;
static uint8_t      zoom     = 1;
static int32_t      if_shift = 0;

static lv_timer_t *timer = NULL;

static void on_zoom_changed(Subject *subj, void *user_data);
static void on_freq_changed(Subject *subj, void *user_data);
static void on_if_shift_changed(Subject *subj, void *user_data);

static void refresh_bands_info();

static void band_info_timer(lv_timer_t *t) {
    lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_TRANSP);
    lv_anim_start(&fade);
    timer = NULL;
}

static void band_info_draw_cb(lv_event_t *e) {
    lv_event_code_t code     = lv_event_get_code(e);
    lv_obj_t       *obj      = lv_event_get_target(e);
    lv_draw_ctx_t  *draw_ctx = lv_event_get_draw_ctx(e);

    if (!band_info) {
        return;
    }

    uint8_t current_zoom = 1;
    if (params.waterfall_zoom.x) {
        current_zoom = zoom;
    }

    lv_coord_t x1 = obj->coords.x1;
    lv_coord_t y1 = obj->coords.y1;

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj) - 1;

    for (uint16_t i = 0; i < bands_count; i++) {
        band_info_t *band = &band_info[i];

        /* Rect */

        lv_border_side_t border_side = LV_BORDER_SIDE_NONE;

        int32_t start = (int64_t)(band->start_freq - freq + if_shift) * w / width_hz * current_zoom;
        int32_t stop  = (int64_t)(band->stop_freq - freq + if_shift) * w / width_hz * current_zoom;

        start += w / 2;
        stop += w / 2;

        if (start < 0) {
            start = 0;
        } else if (start > w) {
            continue;
        } else {
            border_side |= LV_BORDER_SIDE_LEFT;
        }

        if (stop < 0) {
            continue;
        } else if (stop > w) {
            stop = w;
        } else {
            border_side |= LV_BORDER_SIDE_RIGHT;
        }

        lv_draw_rect_dsc_t rect_dsc;
        lv_area_t          area;

        lv_draw_rect_dsc_init(&rect_dsc);

        rect_dsc.bg_color     = bg_color;
        rect_dsc.bg_opa       = LV_OPA_50;
        rect_dsc.border_width = 2;
        rect_dsc.border_color = lv_color_white();
        rect_dsc.border_opa   = LV_OPA_50;
        rect_dsc.border_side  = border_side;

        area.x1 = x1 + start + 2;
        area.y1 = y1;
        area.x2 = x1 + stop - 2;
        area.y2 = y1 + h;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);

        /* Label */

        lv_draw_label_dsc_t dsc_label;
        lv_draw_label_dsc_init(&dsc_label);

        dsc_label.color = lv_color_white();
        dsc_label.font  = &sony_22;

        lv_point_t label_size;
        lv_txt_get_size(&label_size, band->name, dsc_label.font, 0, 0, LV_COORD_MAX, 0);

        if (stop - start > label_size.x) {
            area.x1 = x1 + (start + stop) / 2 - label_size.x / 2;
            area.y1 = y1 + (h - label_size.y) / 2;
            area.x2 = x1 + (start + stop / 2 + label_size.x / 2);
            area.y2 = y1 + h;

            lv_draw_label(draw_ctx, &dsc_label, &area, band->name, NULL);
        }
    }
}

static void fade_anim(void *obj, int32_t v) {
    lv_obj_set_style_opa_layered(obj, v, 0);
}

static void fade_ready(lv_anim_t *a) {
    fade_run = false;
}

lv_obj_t *band_info_init(lv_obj_t *parent) {
    band_info = malloc(sizeof(*band_info) * band_info_cap);
    refresh_bands_info();
    obj = lv_obj_create(parent);

    lv_obj_set_size(obj, lv_obj_get_width(parent), band_info_height);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, -lv_obj_get_height(parent) / 2 + 18);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_0, 0);

    lv_obj_add_event_cb(obj, band_info_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_time(&fade, 250);
    lv_anim_set_exec_cb(&fade, fade_anim);
    lv_anim_set_ready_cb(&fade, fade_ready);

    subject_add_observer_and_call(cfg_cur.zoom, on_zoom_changed, NULL);
    subject_add_delayed_observer_and_call(cfg_cur.fg_freq, on_freq_changed, NULL);
    subject_add_delayed_observer_and_call(cfg_cur.band->if_shift.val, on_if_shift_changed, NULL);

    return obj;
}

void band_info_update(int32_t f) {
    freq  = f;

    lv_obj_invalidate(obj);

    if (!fade_run) {
        fade_run = true;
        lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_COVER);
        lv_anim_start(&fade);
    }

    if (timer) {
        lv_timer_reset(timer);
    } else {
        timer = lv_timer_create(band_info_timer, 2000, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom = subject_get_int(subj);
}

static void on_freq_changed(Subject *subj, void *user_data) {
    band_info_update(subject_get_int(subj));
}

static void on_if_shift_changed(Subject *subj, void *user_data) {
    if_shift = subject_get_int(subj);
}

static void refresh_bands_info() {
    for (size_t i = 0; i < bands_count; i++) {
        if (band_info[i].name != NULL) {
            free(band_info[i].name);
        }
    }
    bands_count = cfg_band_read_all_bands(&band_info, &band_info_cap);
}
