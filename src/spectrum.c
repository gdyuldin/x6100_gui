/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "spectrum.h"

#include "dsp.h"
#include "events.h"
#include "meter.h"
#include "params/params.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "recorder.h"
#include "rtty.h"
#include "scheduler.h"
#include "styles.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>

#define DEFAULT_MIN S4
#define DEFAULT_MAX S9_20
#define VISOR_HEIGHT_TX (100 - 61)
#define VISOR_HEIGHT_RX 100
#define SPECTRUM_SIZE 800

typedef struct {
    float    val;
    uint64_t time;
} peak_t;

static float grid_min = DEFAULT_MIN;
static float grid_max = DEFAULT_MAX;

static lv_obj_t *obj;

static int32_t width_hz     = 100000;
static int16_t visor_height = 100;

static float   spectrum_buf[SPECTRUM_SIZE];
static peak_t  spectrum_peak[SPECTRUM_SIZE];
static uint8_t zoom_factor = 1;

static bool spectrum_tx = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static x6100_mode_t cur_mode;
static int32_t lo_offset;

static int32_t dnf_enabled = false;
static int32_t dnf_auto;
static int32_t dnf_center;
static int32_t dnf_width;

static int32_t cur_freq;
static int16_t freq_mod;



static pthread_mutex_t data_mux;

static void on_zoom_changed(Subject *subj, void *user_data);
static void on_real_filter_from_change(Subject *subj, void *user_data);
static void on_real_filter_to_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);
static void on_lo_offset_change(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);
static void on_int32_val_change(Subject *subj, void *user_data);
static void on_cur_freq_change(Subject *subj, void *user_data);

static void spectrum_draw_cb(lv_event_t *e) {
    lv_obj_t          *obj      = lv_event_get_target(e);
    lv_draw_ctx_t     *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t main_line_dsc;
    lv_draw_line_dsc_t peak_line_dsc;

    if (!spectrum_buf) {
        return;
    }
    float min, max;
    if (spectrum_tx) {
        min = DEFAULT_MIN;
        max = DEFAULT_MAX;
    } else {
        min = grid_min;
        max = grid_max;
    }

    /* Lines */

    lv_draw_line_dsc_init(&main_line_dsc);

    main_line_dsc.color = lv_color_hex(0x00B300);
    main_line_dsc.width = 1;

    lv_draw_line_dsc_init(&peak_line_dsc);

    peak_line_dsc.color = lv_color_hex(0x555555);
    peak_line_dsc.width = 1;

    lv_coord_t x1 = obj->coords.x1;
    lv_coord_t y1 = obj->coords.y1;

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);

    x1 += lo_offset * zoom_factor * w / width_hz;

    lv_point_t main_a, main_b;
    lv_point_t peak_a, peak_b;

    if (!params.spectrum_filled) {
        main_b.x = x1;
        main_b.y = y1 + h;
    }

    peak_b.x = x1;
    peak_b.y = y1 + h;

    for (uint16_t i = 0; i < SPECTRUM_SIZE; i++) {
        float    v = (spectrum_buf[i] - min) / (max - min);
        uint16_t x = i * w / SPECTRUM_SIZE;

        /* Peak */

        if (params.spectrum_peak && !spectrum_tx) {
            float v_peak = (spectrum_peak[i].val - min) / (max - min);

            peak_a.x = x1 + x;
            peak_a.y = y1 + (1.0f - v_peak) * h;

            lv_draw_line(draw_ctx, &peak_line_dsc, &peak_a, &peak_b);

            peak_b = peak_a;
        }

        /* Main */

        main_a.x = x1 + x;
        main_a.y = y1 + (1.0f - v) * h;

        if (params.spectrum_filled) {
            main_b.x = main_a.x;
            main_b.y = y1 + h;
        }

        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        if (!params.spectrum_filled) {
            main_b = main_a;
        }
    }

    /* Filter */

    lv_draw_rect_dsc_t rect_dsc;
    lv_area_t          area;

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_color = lv_color_hex(0xFFEA00);
    rect_dsc.bg_opa   = LV_OPA_50;

    int32_t w_hz = width_hz / zoom_factor;

    int16_t sign_from = (filter_from > 0) ? 1 : -1;
    int16_t sign_to   = (filter_to > 0) ? 1 : -1;

    int32_t f1 = (w * filter_from) / w_hz;
    int32_t f2 = (w * filter_to) / w_hz;

    area.x1 = x1 + w / 2 + f1;
    area.y1 = y1 + h - visor_height;
    area.x2 = x1 + w / 2 + f2;
    area.y2 = y1 + h;

    lv_draw_rect(draw_ctx, &rect_dsc, &area);

    /* Notch */
    if (dnf_enabled && !dnf_auto && ((cur_mode != x6100_mode_am) && (cur_mode != x6100_mode_nfm))) {
        int32_t from, to;

        rect_dsc.bg_color = lv_color_white();
        rect_dsc.bg_opa   = LV_OPA_50;

        from = sign_from * (dnf_center - dnf_width);
        to   = sign_to * (dnf_center + dnf_width);

        if (from < to) {
            f1 = (w * from) / w_hz;
            f2 = (w * to) / w_hz;
        } else {
            f1 = (w * to) / w_hz;
            f2 = (w * from) / w_hz;
        }

        area.x1 = x1 + w / 2 + f1;
        area.y1 = y1 + h - visor_height;
        area.x2 = x1 + w / 2 + f2;
        area.y2 = y1 + h;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    if (rtty_get_state() != RTTY_OFF) {
        int32_t from, to;

        from = sign_from * (params.rtty_center - params.rtty_shift / 2);
        to   = sign_to * (params.rtty_center + params.rtty_shift / 2);

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        main_a.x = x1 + w / 2 + f1;
        main_a.y = y1 + h - visor_height;
        main_b.x = main_a.x;
        main_b.y = y1 + h;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        main_a.x = x1 + w / 2 + f2;
        main_b.x = main_a.x;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

    /* Center */

    main_line_dsc.width = 1;

    main_a.x = x1 + w / 2;
    main_a.y = y1 + h - visor_height;
    main_b.x = main_a.x;
    main_b.y = y1 + h;

    if (recorder_is_on()) {
        main_line_dsc.color = lv_color_hex(0xFF0000);
    } else if (cur_mode == x6100_mode_cw || cur_mode == x6100_mode_cwr) {
        // Hide LO line on CW
        main_line_dsc.opa = LV_OPA_0;
    }

    lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
}

static void tx_cb(lv_event_t *e) {
    visor_height = VISOR_HEIGHT_TX;
}

static void rx_cb(lv_event_t *e) {
    visor_height = VISOR_HEIGHT_RX;
}

static void spectrum_refresh(void *data) {
    lv_obj_invalidate(obj);
}

lv_obj_t *spectrum_init(lv_obj_t *parent) {
    pthread_mutex_init(&data_mux, NULL);
    spectrum_min_max_reset();

    for (size_t i = 0; i < SPECTRUM_SIZE; i++) {
        spectrum_peak[i].val = S_MIN;
        spectrum_buf[i] = S_MIN;
    }

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &spectrum_style, 0);
    lv_obj_add_event_cb(obj, spectrum_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(obj, tx_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, rx_cb, EVENT_RADIO_RX, NULL);

    subject_add_observer_and_call(cfg_cur.zoom, on_zoom_changed, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.from, on_real_filter_from_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.to, on_real_filter_to_change, NULL);
    subject_add_observer_and_call(cfg_cur.mode, on_cur_mode_change, NULL);
    subject_add_observer_and_call(cfg_cur.lo_offset, on_lo_offset_change, NULL);

    subject_add_observer(cfg.auto_level_enabled.val, on_grid_min_change, NULL);
    subject_add_observer_and_call(cfg_cur.band->grid.min.val, on_grid_min_change, NULL);
    subject_add_observer(cfg.auto_level_enabled.val, on_grid_max_change, NULL);
    subject_add_observer_and_call(cfg_cur.band->grid.max.val, on_grid_max_change, NULL);

    subject_add_observer_and_call(cfg.dnf.val, on_int32_val_change, &dnf_enabled);
    subject_add_observer_and_call(cfg.dnf_auto.val, on_int32_val_change, &dnf_auto);
    subject_add_observer_and_call(cfg.dnf_center.val, on_int32_val_change, &dnf_center);
    subject_add_observer_and_call(cfg.dnf_width.val, on_int32_val_change, &dnf_width);

    subject_add_observer_and_call(cfg_cur.fg_freq, on_cur_freq_change, NULL);
    return obj;
}

void spectrum_data(float *data_buf, uint16_t size, bool tx) {
    uint64_t now = get_time();

    pthread_mutex_lock(&data_mux);
    spectrum_tx = tx;
    for (uint16_t i = 0; i < size; i++) {
        spectrum_buf[i] = data_buf[i];

        if (params.spectrum_peak && !tx) {
            float   v    = spectrum_buf[i];
            peak_t *peak = &spectrum_peak[i];

            if (v > peak->val) {
                peak->time = now;
                peak->val  = v;
            } else {
                if (now - peak->time > params.spectrum_peak_hold) {
                    peak->val -= params.spectrum_peak_speed;
                }
            }
        }
    }

    pthread_mutex_unlock(&data_mux);
    scheduler_put_noargs(spectrum_refresh);
}

void spectrum_min_max_reset() {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        grid_min = DEFAULT_MIN;
        grid_max = DEFAULT_MAX;
    } else {
        grid_min = subject_get_int(cfg_cur.band->grid.min.val);
        grid_max = subject_get_int(cfg_cur.band->grid.max.val);
    }
}

float spectrum_get_min() {
    return grid_min;
}

void spectrum_update_max(float db) {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        lpf(&grid_max, db, 0.75f, DEFAULT_MAX);
        grid_max -= subject_get_float(cfg.auto_level_offset.val);
    }
}

void spectrum_update_min(float db) {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        lpf(&grid_min, db, 0.75f, DEFAULT_MIN);
        grid_min -= subject_get_float(cfg.auto_level_offset.val);
    }
}

void spectrum_clear() {
    spectrum_min_max_reset();
    freq_mod = 0;
    uint64_t now = get_time();

    for (uint16_t i = 0; i < SPECTRUM_SIZE; i++) {
        spectrum_buf[i]       = S_MIN;
        spectrum_peak[i].val  = S_MIN;
        spectrum_peak[i].time = now;
    }
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom_factor = (uint8_t)subject_get_int(subj);
    spectrum_clear();
}

static void on_real_filter_from_change(Subject *subj, void *user_data) {
    filter_from = subject_get_int(subj);
}

static void on_real_filter_to_change(Subject *subj, void *user_data) {
    filter_to = subject_get_int(subj);
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    cur_mode = (x6100_mode_t)subject_get_int(subj);
}

static void on_lo_offset_change(Subject *subj, void *user_data) {
    lo_offset = subject_get_int(subj);
}
static void on_grid_min_change(Subject *subj, void *user_data) {
    if (!subject_get_int(cfg.auto_level_enabled.val)) {
        grid_min = subject_get_int(cfg_cur.band->grid.min.val);
    }
}
static void on_grid_max_change(Subject *subj, void *user_data) {
    if (!subject_get_int(cfg.auto_level_enabled.val)) {
        grid_max = subject_get_int(cfg_cur.band->grid.max.val);
    }
}

static void on_int32_val_change(Subject *subj, void *user_data) {
    *(int32_t*)user_data = subject_get_int(subj);
}

void on_cur_freq_change(Subject *subj, void *user_data) {
    int32_t new_freq = subject_get_int(subj);
    if (cur_freq != new_freq) {
        int32_t df = new_freq - cur_freq + freq_mod;
        cur_freq = new_freq;
        uint64_t time = get_time();

        uint16_t div     = width_hz / SPECTRUM_SIZE / zoom_factor;
        int32_t  delta   = (df + div / 2) / div;
        freq_mod = df - delta * div;

        if (delta == 0) {
            return;
        }
        peak_t  *to;
        for (int16_t i = 0; i < SPECTRUM_SIZE; i++) {
            int16_t dst_id = delta > 0 ? i : SPECTRUM_SIZE - i - 1;
            to = &spectrum_peak[dst_id];
            int16_t src_id = dst_id + delta;
            if ((src_id < 0) || (src_id >= SPECTRUM_SIZE)) {
                to->val = S_MIN;
                to->time = time;
            } else {
                *to = spectrum_peak[src_id];
            }
        }
    }
}
