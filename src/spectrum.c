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
#include "cfg/cfg_api.h"
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
static int32_t fg_freq;
static int32_t rit;
static int32_t mode_lo_offset;
static int32_t if_shift;

static int32_t dnf_show = false;
static int32_t dnf_center;
static int32_t dnf_width;

static bool center_line_show = true;

static int32_t cur_base_lo_freq;
static uint8_t prev_fft_dec = 1;
static int16_t freq_mod;


static pthread_mutex_t data_mux;

static void on_zoom_changed(Subject *subj, void *user_data);
static void update_filters(Subject *subj, void *user_data);
static void update_dnf(Subject *subj, void *user_data);
static void update_center_line(Subject *subj, void *user_data);
static void on_mode_lo_offset_change(Subject *subj, void *user_data);
static void on_if_shift_change(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);
static void on_cur_base_lo_freq_change(Subject *subj, void *user_data);
static void on_rit_change(Subject *subj, void *user_data);
static void shift_peaks(int32_t df);

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

    if (params.spectrum_r.x == 0 && params.spectrum_g.x == 0 && params.spectrum_b.x == 0) {
        main_line_dsc.color = lv_color_hex(0xAAAAAA);
    } else {
        main_line_dsc.color = lv_color_make(params.spectrum_r.x, params.spectrum_g.x, params.spectrum_b.x);
    }
    main_line_dsc.width = 1;

    lv_draw_line_dsc_init(&peak_line_dsc);

    peak_line_dsc.color = lv_color_hex(0x555555);
    peak_line_dsc.width = 1;

    lv_coord_t x1 = obj->coords.x1;
    lv_coord_t y1 = obj->coords.y1;

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);

    lv_coord_t spectrum_offset, markers_offset;
    // spectrum_offset: shift for the spectrum data
    // markers_offset: shift for filter data, notch, etc
    markers_offset = ((mode_lo_offset + if_shift) * zoom_factor * w + width_hz / 2) / width_hz;
    if (spectrum_tx) {
        spectrum_offset = markers_offset;
    } else {
        lv_coord_t data_shift = 0;
        if (cur_base_lo_freq) {
            // Handle delay between sending new settings to base and new data flow
            data_shift = cur_base_lo_freq - (fg_freq + mode_lo_offset - if_shift + rit);
        }
        spectrum_offset = ((mode_lo_offset + data_shift) * zoom_factor * w + width_hz / 2) / width_hz;
    }

    lv_point_t main_a, main_b;
    lv_point_t peak_a, peak_b;

    if (!params.spectrum_filled.x) {
        main_b.x = x1;
        main_b.y = y1 + h;
    }

    peak_b.x = x1;
    peak_b.y = y1 + h;

    for (uint16_t i = 0; i < SPECTRUM_SIZE; i++) {
        float    v = (spectrum_buf[i] - min) / (max - min);
        uint16_t x = i * w / SPECTRUM_SIZE;

        /* Peak */

        if (params.spectrum_peak.x && !spectrum_tx) {
            float v_peak = (spectrum_peak[i].val - min) / (max - min);

            peak_a.x = x1 + spectrum_offset + x;
            peak_a.y = y1 + (1.0f - v_peak) * h;

            lv_draw_line(draw_ctx, &peak_line_dsc, &peak_a, &peak_b);

            peak_b = peak_a;
        }

        /* Main */

        main_a.x = x1 + spectrum_offset + x;
        main_a.y = y1 + (1.0f - v) * h;

        if (params.spectrum_filled.x) {
            main_b.x = main_a.x;
            main_b.y = y1 + h;
        }

        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        if (!params.spectrum_filled.x) {
            main_b = main_a;
        }
    }

    /* Filter */

    lv_draw_rect_dsc_t rect_dsc;
    lv_area_t          area;

    lv_draw_rect_dsc_init(&rect_dsc);

    if (params.spectrum_r.x == 0 && params.spectrum_g.x == 0 && params.spectrum_b.x == 0) {
        rect_dsc.bg_color = bg_color;
    } else {
        rect_dsc.bg_color = lv_color_make(params.spectrum_r.x, params.spectrum_g.x, params.spectrum_b.x);
    }
    rect_dsc.bg_opa   = LV_OPA_50;

    int32_t w_hz = width_hz / zoom_factor;

    int16_t sign_from = (filter_from > 0) ? 1 : -1;
    int16_t sign_to   = (filter_to > 0) ? 1 : -1;

    int32_t f1 = (float)(w * filter_from) / w_hz + 1.0f;
    int32_t f2 = (float)(w * filter_to) / w_hz + 1.0f;


    area.x1 = x1 + markers_offset + w / 2 + f1;
    area.y1 = y1;
    area.x2 = x1 + markers_offset + w / 2 + f2;
    area.y2 = y1 + h;

    lv_draw_rect(draw_ctx, &rect_dsc, &area);

    /* Notch */
    if (dnf_show) {
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

        area.x1 = x1 + markers_offset + w / 2 + f1;
        area.y1 = y1;
        area.x2 = x1 + markers_offset + w / 2 + f2;
        area.y2 = y1 + h;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    if (rtty_get_state() != RTTY_OFF) {
        int32_t from, to;

        from = sign_from * (params.rtty_center - params.rtty_shift / 2);
        to   = sign_to * (params.rtty_center + params.rtty_shift / 2);

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        main_a.x = x1 + markers_offset + w / 2 + f1;
        main_a.y = y1;
        main_b.x = main_a.x;
        main_b.y = y1 + h;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        main_a.x = x1 + markers_offset + w / 2 + f2;
        main_b.x = main_a.x;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

    /* Center */

    main_line_dsc.width = 1;

    main_a.x = x1 + markers_offset + w / 2;
    main_a.y = y1;
    main_b.x = main_a.x;
    main_b.y = y1 + h;

    if (recorder_is_on()) {
        main_line_dsc.color = lv_color_hex(0xFF0000);
    }
    if (center_line_show) {
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

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

    subject_subscribe_and_notify((Subject*)cfg_mode_zoom, on_zoom_changed, NULL);

    subject_subscribe((Subject*)cfg_cur_filter_low, update_filters, NULL);
    subject_subscribe((Subject*)cfg_cur_filter_high, update_filters, NULL);
    subject_subscribe_and_notify((Subject*)cfg_cur_mode, update_filters, NULL);

    subject_subscribe_and_notify((Subject*)cfg_cur_mode, update_center_line, NULL);
    subject_subscribe_and_notify((Subject*)cfg_mode_lo_offset, on_mode_lo_offset_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_if_shift, on_if_shift_change, NULL);

    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_min_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_min, on_grid_min_change, NULL);
    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_max_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_max, on_grid_max_change, NULL);

    subject_subscribe((Subject*)cfg_cur_mode, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf_auto, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf_center, update_dnf, NULL);
    subject_subscribe_and_notify((Subject*)cfg_dnf_width, update_dnf, NULL);

    subject_subscribe_and_notify((Subject*)cfg_fg_freq, on_cur_base_lo_freq_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_rit, on_rit_change, NULL);
    return obj;
}

void spectrum_data(float *data_buf, uint16_t size, bool tx, uint32_t base_lo_freq, uint8_t fft_dec) {
    uint64_t now = get_time();

    if (base_lo_freq != cur_base_lo_freq) {
        int32_t df = base_lo_freq - cur_base_lo_freq;
        cur_base_lo_freq = base_lo_freq;
        shift_peaks(df);
    }

    pthread_mutex_lock(&data_mux);
    spectrum_tx = tx;
    for (uint16_t i = 0; i < size; i++) {
        spectrum_buf[i] = data_buf[i];

        if (params.spectrum_peak.x && !tx) {
            float   v    = spectrum_buf[i];
            peak_t *peak = &spectrum_peak[i];

            if ((v > peak->val) || (fft_dec != prev_fft_dec)) {
                peak->time = now;
                peak->val  = v;
            } else {
                if (now - peak->time > (int)params.spectrum_peak_hold.x * 1000) {
                    peak->val -= params.spectrum_peak_speed.x * 0.1f;
                }
            }
        }
    }
    prev_fft_dec = fft_dec;

    pthread_mutex_unlock(&data_mux);
    scheduler_put_noargs(spectrum_refresh);
}

void spectrum_min_max_reset() {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = DEFAULT_MIN;
        grid_max = DEFAULT_MAX;
    } else {
        grid_min = param_i_get(cfg_band_grid_min);
        grid_max = param_i_get(cfg_band_grid_max);
    }
}

void spectrum_update_max(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_max = db - param_f_get(cfg_auto_level_offset);
    }
}

void spectrum_update_min(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = db - param_f_get(cfg_auto_level_offset);
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
    zoom_factor = (uint8_t)subject_i_get((SubjectInt*)subj);
    spectrum_clear();
}

static void update_filters(Subject *subj, void *user_data) {
    int32_t low = subject_i_get((SubjectInt*)cfg_cur_filter_low);
    int32_t high = subject_i_get((SubjectInt*)cfg_cur_filter_high);
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg_cur_mode);
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
    case x6100_mode_cwr:
        filter_to = -low;
        filter_from = -high;
        break;
    case x6100_mode_am:
    case x6100_mode_nfm:
        filter_from = -high;
        filter_to = high;
        break;

    default:
        filter_from = low;
        filter_to = high;
        break;
    }
}

static void update_dnf(Subject *subj, void *user_data) {
    int32_t en = subject_i_get((SubjectInt*)cfg_dnf);
    if (!en) {
        return;
    }
    int32_t auto_ = subject_i_get((SubjectInt*)cfg_dnf_auto);
    if (auto_) {
        return;
    }
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg_cur_mode);
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        return;
    }
    dnf_width = subject_i_get((SubjectInt*)cfg_dnf_width);
    int32_t center = subject_i_get((SubjectInt*)cfg_dnf_auto);
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
    case x6100_mode_cwr:
        dnf_center = -center;
        break;

    default:
        dnf_center = center;
        break;
    }
}


static void update_center_line(Subject *subj, void *user_data) {
    x6100_mode_t mode = (x6100_mode_t)subject_i_get((SubjectInt*)subj);
    center_line_show = (mode != x6100_mode_cw && mode != x6100_mode_cwr);
}

static void on_mode_lo_offset_change(Subject *subj, void *user_data) {
    mode_lo_offset = subject_i_get((SubjectInt*)subj);
}

static void on_if_shift_change(Subject *subj, void *user_data) {
    if_shift = subject_i_get((SubjectInt*)subj);
}

static void on_grid_min_change(Subject *subj, void *user_data) {
    if (!param_i_get(cfg_auto_level_enabled)) {
        grid_min = param_i_get(cfg_band_grid_min);
    }
}
static void on_grid_max_change(Subject *subj, void *user_data) {
    if (!param_i_get(cfg_auto_level_enabled)) {
        grid_max = param_i_get(cfg_band_grid_max);
    }
}

static void on_cur_base_lo_freq_change(Subject *subj, void *user_data) {
    fg_freq = cparam_i_get(cfg_fg_freq);
    scheduler_put_noargs(spectrum_refresh);
}

static void on_rit_change(Subject *subj, void *user_data) {
    rit = param_i_get(cfg_rit);
}

static void shift_peaks(int32_t df) {
    df += freq_mod;
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
