/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "spectrum.h"

#include "globals.h"
#include "dsp.h"
#include "events.h"
#include "meter.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "recorder.h"
#include "rtty.h"
#include "scheduler.h"
#include "styles.h"
#include "util.h"

#include "lv_drivers/display/drm.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define VISOR_HEIGHT_TX (100 - 61)
#define VISOR_HEIGHT_RX 100
#define SPECTRUM_SIZE SCREEN_WIDTH

typedef struct {
    float    val;
    uint64_t time;
} peak_t;

/* Display levels for the current frame, resolved by the DSP module (auto /
 * manual / offset / TX). Written from the DSP thread in spectrum_data() and
 * read by the render thread. */
static float spectrum_min = S_MIN;
static float spectrum_max = S9_40;

/* Peak config cached from cfg via subscription and read by the DSP thread in
 * spectrum_data(). Atomic so the per-bin loop avoids a Subject mutex lock per
 * bin; defaults match p_spectrum_peak / peak_hold / peak_speed. */
static int s_peak_enabled = 1;
static int s_peak_hold    = 5;
static int s_peak_speed   = 5;

static lv_obj_t *obj;

static int32_t width_hz     = 100000;

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

/* Direct-render state. At render time the actual physical geometry (x offset in columns, strip width) is derived from the spectrum object's logical coords:
 *   s_spec_x  = obj->coords.y1          (logical y  → physical x)
 *   s_spec_w  = lv_obj_get_height(obj)  (logical h  → physical width)
 *   s_spec_h  = SPECTRUM_SIZE           (logical w  → physical height) */
static int16_t   s_spec_x;
static int16_t   s_spec_w;

static lv_color_t s_main_color;
static lv_color_t s_peak_color;
static lv_grad_dsc_t grad_dsc;

/* Cross-thread flags set by the DSP thread (spectrum_data) / config callbacks
 * and consumed by spectrum_process() on the main thread. */
static int s_data_ready = 0;
static int s_cond_dirty = 1;

static void on_zoom_changed(Subject *subj, void *user_data);
static void update_filters(Subject *subj, void *user_data);
static void update_dnf(Subject *subj, void *user_data);
static void update_center_line(Subject *subj, void *user_data);
static void on_mode_lo_offset_change(Subject *subj, void *user_data);
static void on_if_shift_change(Subject *subj, void *user_data);
static void on_fg_freq_change(Subject *subj, void *user_data);
static void on_rit_change(Subject *subj, void *user_data);
static void on_peak_changed(Subject *subj, void *user_data);
static void on_peak_hold_changed(Subject *subj, void *user_data);
static void on_peak_speed_changed(Subject *subj, void *user_data);
static void shift_peaks(int32_t df);

static void spectrum_render_rotated(uint32_t *buf, int stride);
static int32_t spectrum_compute_offset(void);
static void spectrum_update_colors(void);
static void spectrum_fill_run(uint32_t *buf, int stride, int row, int x_top, lv_grad_t *grad);
static void spectrum_draw_polyline(uint32_t *buf, int stride, float min, float max, int32_t offset, bool is_peak, lv_color_t color);
static void spectrum_wu_line(uint32_t *buf, int stride, float x0, float y0, float x1, float y1, lv_color_t color);
static void spectrum_blend_px(uint32_t *buf, int stride, int x, int y, float brightness, lv_color_t color);


static lv_coord_t spectrum_markers_offset(void) {
    return ((mode_lo_offset + if_shift) * zoom_factor * SPECTRUM_SIZE + width_hz / 2) / width_hz;
}

static void spectrum_overlay_draw_cb(lv_event_t *e) {
    lv_obj_t          *obj      = lv_event_get_target(e);
    lv_draw_ctx_t     *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t line_dsc;
    lv_draw_rect_dsc_t rect_dsc;

    lv_coord_t x1 = obj->coords.x1;
    // TODO: move offset to configuration
    lv_coord_t y1 = obj->coords.y1 + 70;
    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = obj->coords.y2 - y1;

    lv_coord_t markers_offset = spectrum_markers_offset();

    /* Filter */

    lv_draw_rect_dsc_init(&rect_dsc);

    if (param_i_get(cfg.spectrum.spectrum_use_custom_color())) {
        rect_dsc.bg_color.full = param_i_get(cfg.spectrum.spectrum_color());
    } else {
        rect_dsc.bg_color = style.colors.mark;
    }
    rect_dsc.bg_opa = LV_OPA_40;

    int32_t w_hz = width_hz / zoom_factor;

    int16_t sign_from = (filter_from > 0) ? 1 : -1;
    int16_t sign_to   = (filter_to > 0) ? 1 : -1;

    int32_t f1 = (float)(w * filter_from) / w_hz + 1.0f;
    int32_t f2 = (float)(w * filter_to) / w_hz + 1.0f;

    lv_area_t area;
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

    /* RTTY markers */

    lv_draw_line_dsc_init(&line_dsc);

    if (param_i_get(cfg.spectrum.spectrum_use_custom_color())) {
        line_dsc.color.full = param_i_get(cfg.spectrum.spectrum_color());
    } else {
        line_dsc.color = lv_color_hex(0xAAAAAA);
    }
    line_dsc.width = 1;

    if (rtty_get_state() != RTTY_OFF) {
        int32_t from, to;

        from = sign_from * (param_i_get(cfg.rtty.center()) - param_i_get(cfg.rtty.shift()) / 2);
        to   = sign_to * (param_i_get(cfg.rtty.center()) + param_i_get(cfg.rtty.shift()) / 2);

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        lv_point_t a, b;

        a.x = x1 + markers_offset + w / 2 + f1;
        a.y = y1;
        b.x = a.x;
        b.y = y1 + h;
        lv_draw_line(draw_ctx, &line_dsc, &a, &b);

        a.x = x1 + markers_offset + w / 2 + f2;
        b.x = a.x;
        lv_draw_line(draw_ctx, &line_dsc, &a, &b);
    }

    /* Center */

    line_dsc.width = 1;

    lv_point_t c;

    c.x = x1 + markers_offset + w / 2;
    c.y = y1;
    lv_point_t d;
    d.x = c.x;
    d.y = y1 + h;

    if (recorder_is_on()) {
        line_dsc.color = lv_color_hex(0xFF0000);
    }
    if (center_line_show) {
        lv_draw_line(draw_ctx, &line_dsc, &c, &d);
    }
}

static uint32_t spectrum_sub_id = DSP_FRAME_SUB_INVALID;

static void spectrum_frame_cb(const dsp_frame_t *frame, void *user_data) {
    (void)user_data;
    spectrum_data(frame->psd_db, frame->size, frame->tx, frame->base_freq, frame->fft_dec, frame->min, frame->max);
}

lv_obj_t *spectrum_init(lv_obj_t *overlay_parent, lv_coord_t y, lv_coord_t h) {
    s_spec_x = y;
    s_spec_w = h;

    for (size_t i = 0; i < SPECTRUM_SIZE; i++) {
        spectrum_peak[i].val = S_MIN;
        spectrum_buf[i]      = S_MIN;
    }

    obj = lv_obj_create(overlay_parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(obj, 0, y);
    lv_obj_set_size(obj, SPECTRUM_SIZE, h);
    lv_obj_add_event_cb(obj, spectrum_overlay_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    // Setup spectrum gradient
    grad_dsc.dir           = LV_GRAD_DIR_HOR;
    grad_dsc.stops_count   = 3;
    grad_dsc.stops[0].frac = 0;
    grad_dsc.stops[1].frac = 128;
    grad_dsc.stops[2].frac = 255;

    subject_subscribe_and_notify((Subject *)cfg.mode.zoom(), on_zoom_changed, NULL);

    subject_subscribe((Subject *)cfg.filter.low(), update_filters, NULL);
    subject_subscribe((Subject *)cfg.filter.high(), update_filters, NULL);
    subject_subscribe_and_notify((Subject *)cfg.computed.mode(), update_filters, NULL);

    subject_subscribe_and_notify((Subject *)cfg.computed.mode(), update_center_line, NULL);
    subject_subscribe_and_notify((Subject *)cfg.computed.mode_lo_offset(), on_mode_lo_offset_change, NULL);
    subject_subscribe_and_notify((Subject *)cfg.band.if_shift(), on_if_shift_change, NULL);

    subject_subscribe((Subject *)cfg.computed.mode(), update_dnf, NULL);
    subject_subscribe((Subject *)cfg.dsp.dnf(), update_dnf, NULL);
    subject_subscribe((Subject *)cfg.dsp.dnf_auto(), update_dnf, NULL);
    subject_subscribe((Subject *)cfg.dsp.dnf_center(), update_dnf, NULL);
    subject_subscribe_and_notify((Subject *)cfg.dsp.dnf_width(), update_dnf, NULL);

    subject_subscribe_and_notify((Subject *)cfg.spectrum.peak(), on_peak_changed, NULL);
    subject_subscribe_and_notify((Subject *)cfg.spectrum.peak_hold(), on_peak_hold_changed, NULL);
    subject_subscribe_and_notify((Subject *)cfg.spectrum.peak_speed(), on_peak_speed_changed, NULL);

    subject_subscribe_and_notify((Subject *)cfg.computed.fg_freq(), on_fg_freq_change, NULL);
    subject_subscribe_and_notify((Subject *)cfg.general.rit(), on_rit_change, NULL);

    if (spectrum_sub_id == DSP_FRAME_SUB_INVALID) {
        spectrum_sub_id = dsp_frame_subscribe(DSP_FRAME_SPECTRUM, spectrum_frame_cb, NULL);
    }

    return obj;
}

void spectrum_data(const float *data_buf, uint16_t size, bool tx, uint32_t base_lo_freq, uint8_t fft_dec, float min, float max) {
    uint64_t now = get_time();

    if (base_lo_freq != cur_base_lo_freq) {
        int32_t df = base_lo_freq - cur_base_lo_freq;
        cur_base_lo_freq = base_lo_freq;
        shift_peaks(df);
    }

    spectrum_tx  = tx;
    spectrum_min = min;
    spectrum_max = max;

    const bool  peak_enabled = __atomic_load_n(&s_peak_enabled, __ATOMIC_ACQUIRE);
    const int   peak_hold    = __atomic_load_n(&s_peak_hold, __ATOMIC_ACQUIRE);
    const float peak_speed   = __atomic_load_n(&s_peak_speed, __ATOMIC_ACQUIRE) * 0.1f;

    for (uint16_t i = 0; i < size; i++) {
        spectrum_buf[i] = data_buf[i];

        if (peak_enabled && !tx) {
            float   v    = spectrum_buf[i];
            peak_t *peak = &spectrum_peak[i];

            if ((v > peak->val) || (fft_dec != prev_fft_dec)) {
                peak->time = now;
                peak->val  = v;
            } else {
                if (now - peak->time > (int)peak_hold * 1000) {
                    peak->val -= peak_speed;
                }
            }
        }
    }
    prev_fft_dec = fft_dec;

    __atomic_store_n(&s_data_ready, 1, __ATOMIC_RELEASE);
}

void spectrum_clear() {
    spectrum_min = S_MIN;
    spectrum_max = S9_40;
    freq_mod     = 0;
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
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
    lv_obj_invalidate(obj);
}

static void update_filters(Subject *subj, void *user_data) {
    int32_t low = subject_i_get((SubjectInt*)cfg.filter.low());
    int32_t high = subject_i_get((SubjectInt*)cfg.filter.high());
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg.computed.mode());
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
    lv_obj_invalidate(obj);
}

static void update_dnf(Subject *subj, void *user_data) {
    int32_t en = subject_i_get((SubjectInt*)cfg.dsp.dnf());
    if (!en) {
        dnf_show = false;
        return;
    }
    int32_t auto_ = subject_i_get((SubjectInt*)cfg.dsp.dnf_auto());
    if (auto_) {
        dnf_show = false;
        return;
    }
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg.computed.mode());
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        dnf_show = false;
        return;
    }
    dnf_show = true;
    dnf_width = subject_i_get((SubjectInt*)cfg.dsp.dnf_width());
    int32_t center = subject_i_get((SubjectInt*)cfg.dsp.dnf_auto());
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
    lv_obj_invalidate(obj);
}


static void update_center_line(Subject *subj, void *user_data) {
    x6100_mode_t mode = (x6100_mode_t)subject_i_get((SubjectInt*) subj);
    center_line_show = (mode != x6100_mode_cw && mode != x6100_mode_cwr);
    lv_obj_invalidate(obj);
}

static void on_mode_lo_offset_change(Subject *subj, void *user_data) {
    mode_lo_offset = subject_i_get((SubjectInt*) subj);
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
    lv_obj_invalidate(obj);
}

static void on_if_shift_change(Subject *subj, void *user_data) {
    if_shift = subject_i_get((SubjectInt*) subj);
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
    lv_obj_invalidate(obj);
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    fg_freq = cparam_i_get(cfg.computed.fg_freq());
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_rit_change(Subject *subj, void *user_data) {
    rit = param_i_get(cfg.general.rit());
}

static void on_peak_changed(Subject *subj, void *user_data) {
    __atomic_store_n(&s_peak_enabled, subject_i_get((SubjectInt *)subj), __ATOMIC_RELEASE);
}

static void on_peak_hold_changed(Subject *subj, void *user_data) {
    __atomic_store_n(&s_peak_hold, subject_i_get((SubjectInt *)subj), __ATOMIC_RELEASE);
}

static void on_peak_speed_changed(Subject *subj, void *user_data) {
    __atomic_store_n(&s_peak_speed, subject_i_get((SubjectInt *)subj), __ATOMIC_RELEASE);
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

/***** Direct (rotated) rendering *****/

static void spectrum_update_colors(void) {
    grad_dsc.stops[2].color = style.colors.spectrum.high;
    grad_dsc.stops[1].color = style.colors.spectrum.mid;
    grad_dsc.stops[0].color = style.colors.spectrum.low;
    s_main_color = style.colors.spectrum.line;
    s_peak_color = style.colors.spectrum.peak;

}

/* Replicate the spectrum_offset calculation from spectrum_draw_cb(). */
static int32_t spectrum_compute_offset(void) {
    int32_t w = SPECTRUM_SIZE;

    if (spectrum_tx) {
        return ((mode_lo_offset + if_shift) * zoom_factor * w + width_hz / 2) / width_hz;
    }

    int32_t data_shift = 0;
    if (cur_base_lo_freq) {
        // Handle delay between sending new settings to base and new data flow
        data_shift = cur_base_lo_freq - (fg_freq + mode_lo_offset - if_shift + rit);
    }
    return ((mode_lo_offset + data_shift) * zoom_factor * w + width_hz / 2) / width_hz;
}

/* Fill one physical row from column x_top to the bottom of the strip. */
static void spectrum_fill_run(uint32_t *buf, int stride, int row, int x_top, lv_grad_t *grad) {
    if (x_top >= stride) {
        return;
    }
    uint32_t *p = &buf[row * stride + x_top];
    int       n = stride - x_top;
    for (int i = 0; i < n; i++) {
        p[i] = grad->map[n - i].full;
    }
}

static inline uint32_t spectrum_blend_xrgb(uint32_t dst, uint32_t src, uint8_t a) {
    uint32_t rb = ((dst & 0x00FF00FFu) * (255u - a) + (src & 0x00FF00FFu) * a + 0x00800080u);
    uint32_t g  = ((dst & 0x0000FF00u) * (255u - a) + (src & 0x0000FF00u) * a + 0x00008000u);
    return ((rb >> 8) & 0x00FF00FFu) | ((g >> 8) & 0x0000FF00u);
}

static void spectrum_blend_px(uint32_t *buf, int stride, int x, int y, float brightness, lv_color_t color) {
    if (x < 0 || x >= stride || y < 0 || y >= SPECTRUM_SIZE) {
        return;
    }
    int a = (int)(brightness * 255.0f + 0.5f);
    if (a <= 0) {
        return;
    }
    if (a > 255) {
        a = 255;
    }
    uint32_t *p = &buf[y * stride + x];
    *p          = spectrum_blend_xrgb(*p, color.full, (uint8_t)a);
}

/* Wu's line algorithm (float).
 *
 * NOTE: do NOT reuse this routine elsewhere. It is specialized for drawing 1px
 * amplitude polylines into the tightly packed, already-rotated spectrum buffer
 * (XRGB8888, row = physical_y = bin, column = physical_x = amplitude). Between
 * adjacent bins |dy| == 1 and the amplitude step varies, so segments are almost
 * always "flat"; the steep/swap handling exists only for degenerate cases and is
 * kept for completeness. */
static void spectrum_wu_line(uint32_t *buf, int stride, float x0, float y0, float x1, float y1, lv_color_t color) {
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);

    if (steep) {
        float t = x0;
        x0      = y0;
        y0      = t;
        t       = x1;
        x1      = y1;
        y1      = t;
    }
    if (x0 > x1) {
        float t = x0;
        x0      = x1;
        x1      = t;
        t       = y0;
        y0      = y1;
        y1      = t;
    }

    float dx       = x1 - x0;
    float dy       = y1 - y0;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    float xend  = roundf(x0);
    float yend  = y0 + gradient * (xend - x0);
    float xgap  = 1.0f - (x0 + 0.5f - floorf(x0 + 0.5f));
    int   xpxl1 = (int)xend;
    int   ypxl1 = (int)floorf(yend);

    if (steep) {
        spectrum_blend_px(buf, stride,ypxl1, xpxl1, (1.0f - (yend - floorf(yend))) * xgap, color);
        spectrum_blend_px(buf, stride,ypxl1 + 1, xpxl1, (yend - floorf(yend)) * xgap, color);
    } else {
        spectrum_blend_px(buf, stride,xpxl1, ypxl1, (1.0f - (yend - floorf(yend))) * xgap, color);
        spectrum_blend_px(buf, stride,xpxl1, ypxl1 + 1, (yend - floorf(yend)) * xgap, color);
    }

    float intery = yend + gradient;

    float xend2  = roundf(x1);
    float yend2  = y1 + gradient * (xend2 - x1);
    float xgap2  = x1 + 0.5f - floorf(x1 + 0.5f);
    int   xpxl2  = (int)xend2;
    int   ypxl2  = (int)floorf(yend2);

    if (steep) {
        spectrum_blend_px(buf, stride,ypxl2, xpxl2, (1.0f - (yend2 - floorf(yend2))) * xgap2, color);
        spectrum_blend_px(buf, stride,ypxl2 + 1, xpxl2, (yend2 - floorf(yend2)) * xgap2, color);
    } else {
        spectrum_blend_px(buf, stride,xpxl2, ypxl2, (1.0f - (yend2 - floorf(yend2))) * xgap2, color);
        spectrum_blend_px(buf, stride,xpxl2, ypxl2 + 1, (yend2 - floorf(yend2)) * xgap2, color);
    }

    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            spectrum_blend_px(buf, stride,(int)floorf(intery), x, 1.0f - (intery - floorf(intery)), color);
            spectrum_blend_px(buf, stride,(int)floorf(intery) + 1, x, intery - floorf(intery), color);
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            spectrum_blend_px(buf, stride,x, (int)floorf(intery), 1.0f - (intery - floorf(intery)), color);
            spectrum_blend_px(buf, stride,x, (int)floorf(intery) + 1, intery - floorf(intery), color);
            intery += gradient;
        }
    }
}

/* Draw the main (or peak) spectrum as a polyline. The initial point is the
 * bottom-left corner of the strip (logical (0, y1+h)), matching the first
 * peak_b/main_b in spectrum_draw_cb(). */
static void spectrum_draw_polyline(uint32_t *buf, int stride, float min, float max, int32_t offset, bool is_peak, lv_color_t color) {
    float prev_x = (float)stride;
    float prev_y = (float)(SPECTRUM_SIZE - 1);

    for (int i = 0; i < SPECTRUM_SIZE; i++) {
        float v = is_peak ? (spectrum_peak[i].val - min) / (max - min) : (spectrum_buf[i] - min) / (max - min);
        if (v < 0.0f) {
            v = 0.0f;
        }
        if (v > 1.0f) {
            v = 1.0f;
        }

        float cur_x = (1.0f - v) * stride;
        float cur_y = (float)(SPECTRUM_SIZE - 1 - offset - i);

        spectrum_wu_line(buf, stride, prev_x, prev_y, cur_x, cur_y, color);

        prev_x = cur_x;
        prev_y = cur_y;
    }
}

static void spectrum_render_rotated(uint32_t *buf, int stride) {
    memset(buf, 0, (size_t)stride * SPECTRUM_SIZE * sizeof(uint32_t));

    float min = spectrum_min;
    float max = spectrum_max;
    if (max <= min) {
        max = min + 1.0f;
    }

    spectrum_update_colors();
    int32_t offset = spectrum_compute_offset();

    if (param_i_get(cfg.spectrum.peak()) && !spectrum_tx) {
        spectrum_draw_polyline(buf, stride, min, max, offset, true, s_peak_color);
    }

    if (param_i_get(cfg.spectrum.filled())) {
        lv_grad_t * cached_grad = lv_gradient_get(&grad_dsc, stride, 1);
        for (int i = 0; i < SPECTRUM_SIZE; i++) {
            int row = SPECTRUM_SIZE - 1 - offset - i;
            if (row < 0 || row >= SPECTRUM_SIZE) {
                continue;
            }
            float v = (spectrum_buf[i] - min) / (max - min);
            if (v < 0.0f) {
                v = 0.0f;
            }
            if (v > 1.0f) {
                v = 1.0f;
            }
            int x_top = (int)((1.0f - v) * stride);
            if (x_top < 0) {
                x_top = 0;
            }
            if (x_top >= stride) {
                x_top = stride - 1;
            }
            spectrum_fill_run(buf, stride, row, x_top, cached_grad);
        }
    }
    spectrum_draw_polyline(buf, stride, min, max, offset, false, s_main_color);
}

/* Called from the main loop (between lv_timer_handler() and drm_flip()) when the
 * direct-render path is enabled. Returns true if a frame was produced. */
bool spectrum_process(void) {
    bool data = __atomic_exchange_n(&s_data_ready, 0, __ATOMIC_ACQUIRE);
    bool cond = __atomic_exchange_n(&s_cond_dirty, 0, __ATOMIC_ACQUIRE);
    if (!data && !cond) {
        return false;
    }

    drm_direct_ctx_t ctx;
    if (!drm_primary_begin_direct(&ctx, (uint32_t)s_spec_w * SPECTRUM_SIZE))
        return false;

    spectrum_render_rotated((uint32_t *)ctx.buf, s_spec_w);

    lv_area_t area = { .x1 = s_spec_x, .y1 = 0,
                       .x2 = s_spec_x + s_spec_w - 1,
                       .y2 = SPECTRUM_SIZE - 1 };
    drm_primary_end_direct(&area);
    return true;
}
