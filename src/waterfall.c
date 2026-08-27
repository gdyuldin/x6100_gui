/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "waterfall.h"

#include "styles.h"
#include "radio.h"
#include "events.h"
#include "params/params.h"
#include "cfg/cfg_api.h"
#include "band_info.h"
#include "meter.h"
#include "backlight.h"
#include "dsp.h"
#include "util.h"
#include "pubsub_ids.h"

#include "lv_drivers/display/drm.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define DEFAULT_MIN S4
#define DEFAULT_MAX S9_20
#define WIDTH 800

typedef struct {
    uint8_t values[WATERFALL_NFFT];
    uint32_t center_freq;
    uint32_t width;
} wf_data_row_t;

static lv_obj_t         *overlay_obj;
static bool             ready = false;

static lv_obj_t         *middle_line;
static lv_point_t       middle_line_points[] = { {0, 0}, {0, 0} };

static int32_t          width_hz = 100000;

static float            grid_min = DEFAULT_MIN;
static float            grid_max = DEFAULT_MAX;

static uint8_t          delay = 0;

static wf_data_row_t    *wf_rows;
static uint16_t         last_row_id;

static int32_t          radio_center_freq = 0;
static int32_t          wf_center_freq = 0;
static int32_t          mode_lo_offset = 0;
static int32_t          if_shift = 0;

static uint8_t          refresh_period = 1;
static uint8_t          refresh_counter = 0;

static uint8_t          zoom = 1;

/* Direct-render state. At render time the actual physical geometry (x offset in
 * columns, strip width) is derived from the arguments passed to waterfall_init():
 *   s_wf_x  = y  (logical y  -> physical x)
 *   s_wf_w  = h  (logical h  -> physical width/stride)
 *   s_wf_h  = WIDTH (800) constant (logical w -> physical height) */
static int16_t   s_wf_x;
static int16_t   s_wf_w;

/* Cross-thread flags set by the DSP thread (waterfall_data) / config callbacks
 * and consumed by waterfall_process() on the main thread. */
static int s_data_ready = 0;
static int s_cond_dirty = 1;

static void update_middle_line();
static void middle_line_cb(lv_event_t * event);
static void on_zoom_changed(Subject *subj, void *user_data);
static void on_fg_freq_change(Subject *subj, void *user_data);
static void on_mode_lo_offset_change(Subject *subj, void *user_data);
static void on_if_shift_changed(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);

void waterfall_init(lv_obj_t * overlay_parent, lv_coord_t y, lv_coord_t h) {
    s_wf_x = y;
    s_wf_w = h;

    waterfall_min_max_reset();

    wf_rows = calloc(h, sizeof(*wf_rows));
    for (size_t i = 0; i < (size_t)h; i++) {
        wf_rows[i].center_freq = radio_center_freq;
        memset(wf_rows[i].values, 0, WATERFALL_NFFT);
    }
    last_row_id = 0;

    overlay_obj = lv_obj_create(overlay_parent);
    lv_obj_remove_style_all(overlay_obj);
    lv_obj_set_style_bg_opa(overlay_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(overlay_obj, lv_color_black(), 0);
    lv_obj_set_pos(overlay_obj, 0, y);
    lv_obj_set_size(overlay_obj, WIDTH, h);

    middle_line = lv_line_create(overlay_obj);
    lv_obj_add_style(middle_line, &style_waterfall_middle_line, 0);
    middle_line_points[1].y = h;
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_add_event_cb(overlay_obj, middle_line_cb, LV_EVENT_DRAW_POST_END, NULL);

    band_info_init(overlay_obj);

    ready = true;

    subject_subscribe_and_notify((Subject*)cfg_fg_freq, on_fg_freq_change, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_mode_zoom, on_zoom_changed, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_band_if_shift, on_if_shift_changed, NULL);
    subject_subscribe_and_notify((Subject*)cfg_mode_lo_offset, on_mode_lo_offset_change, NULL);
    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_min_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_min, on_grid_min_change, NULL);
    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_max_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_max, on_grid_max_change, NULL);
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % s_wf_w;
}

void waterfall_data(float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint32_t width_hz) {
    if (!ready) {
        return;
    }
    if (delay && (base_freq == 0))
    {
        delay--;
        return;
    }
    scroll_down();

    float min, max;
    if (tx) {
        min = DEFAULT_MIN;
        max = DEFAULT_MAX;
    } else {
        min = grid_min;
        max = grid_max;
    }
    if (base_freq == 0) {
        base_freq = radio_center_freq + mode_lo_offset;
    } else if (tx) {
        // New patched firmware
        base_freq += mode_lo_offset;
    }
    wf_rows[last_row_id].center_freq = base_freq;
    wf_rows[last_row_id].width = width_hz;

    float temp_buf[size];
    liquid_vectorf_addscalar(data_buf, size, -min, temp_buf);
    liquid_vectorf_mulscalar(temp_buf, size, 255.0f / (max - min), temp_buf);
    for (uint16_t x = 0; x < size; x++) {
        float   v = temp_buf[x];
        uint8_t id;

        if (v < 0.0f) {
            id = 0;
        } else if (v > 255.0f) {
            id = 255;
        } else {
            id = v;
        }

        wf_rows[last_row_id].values[x] = id;
    }

    refresh_counter++;
    if (refresh_counter >= refresh_period) {
        refresh_counter = 0;
        __atomic_store_n(&s_data_ready, 1, __ATOMIC_RELEASE);
    }
}

void waterfall_min_max_reset() {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = DEFAULT_MIN;
        grid_max = DEFAULT_MAX;
    } else {
        grid_min = param_i_get(cfg_band_grid_min);
        grid_max = param_i_get(cfg_band_grid_max);
    }
}

void waterfall_update_max(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_max = db - param_f_get(cfg_auto_level_offset);
    }
}

void waterfall_update_min(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = db - param_f_get(cfg_auto_level_offset);
    }
}

void waterfall_refresh_reset() {
    refresh_period = 1;
}

void waterfall_refresh_period_set(uint8_t k) {
    if (k == 0) {
        return;
    }
    refresh_period = k;
}


#define LERP_INTERP_M      3
#define LERP_INTERP_FRAC   (1 << LERP_INTERP_M)                 // 8
#define SCALE              (WATERFALL_NFFT * LERP_INTERP_FRAC)  // 8192
#define MAX_SRC_POS        ((WATERFALL_NFFT - 2) * LERP_INTERP_FRAC)
/**
 * Render one waterfall row directly into one rotated-buffer column using integer
 * DDA stepping.
 *
 * Eliminates the two per-pixel 32-bit multiplications and two 32-bit divisions
 * from the original lerp_row by precomputing the rational step and using a
 * Bresenham-style accumulator. Also writes palette result directly into
 * buf[(WIDTH - 1 - i) * stride + col], removing the intermediate dst[800]
 * buffer and its separate copy loop.
 */
static void lerp_row_to_col(const wf_data_row_t *row_data, uint32_t dst_center_freq,
                            uint32_t dst_width_hz, uint32_t *buf, int stride, int col)
{
    if (!row_data->width) {
        for (size_t i = 0; i < WIDTH; i++) {
            buf[(WIDTH - 1 - i) * stride + col] = 0xFF000000;
        }
        return;
    }

    const int32_t src_start = (int32_t)(row_data->center_freq - row_data->width / 2);
    const int32_t src_end   = src_start + (int32_t)row_data->width;

    const int32_t dst_start = (int32_t)(dst_center_freq - dst_width_hz / 2);
    const int32_t dst_end   = dst_start + (int32_t)dst_width_hz;

    if ((src_start > dst_end) || (src_end < dst_start)) {
        for (size_t i = 0; i < WIDTH; i++) {
            buf[(WIDTH - 1 - i) * stride + col] = 0xFF000000;
        }
        return;
    }

    const int32_t dst_half   = (int32_t)(dst_width_hz / 2);
    const int32_t freq_start = (int32_t)dst_center_freq - dst_half;
    const int32_t src_width  = (int32_t)row_data->width;

    /*
     * src_pos[i] = (OFFSET + STEP * i) / DENOM    (exact rational)
     *
     *   STEP   = dst_width_hz * SCALE
     *   DENOM  = WIDTH * src_width
     *   OFFSET = ((freq_start - src_start) * WIDTH + dst_half) * SCALE
     *
     * DDA:  accum += STEP  →  src_pos += accum / DENOM;  accum %= DENOM
     *       Pre-split STEP into quotient q=STEP/DENOM and remainder r=STEP%DENOM
     *       so the inner loop contains only additions and comparisons.
     */
    const int32_t DENOM = WIDTH * src_width;
    const int32_t STEP  = (int32_t)dst_width_hz * SCALE;

    /* 64-bit intermediate — harmless: one divmod per row, not per pixel */
    const int64_t OFFSET = ((int64_t)(freq_start - src_start) * WIDTH + dst_half) * SCALE;

    int64_t accum   = OFFSET % DENOM;
    int32_t src_pos = (int32_t)(OFFSET / DENOM);

    /* Normalize accumulator to [0, DENOM) for unsigned-style stepping */
    if (accum < 0) {
        accum += DENOM;
        src_pos--;
    }

    const int32_t step_q = STEP / DENOM;
    const int32_t step_r = STEP % DENOM;

    for (size_t i = 0; i < WIDTH; i++) {
        if (src_pos < 0 || src_pos > MAX_SRC_POS) {
            buf[(WIDTH - 1 - i) * stride + col] = 0xFF000000;
        } else {
            const uint32_t idx  = (uint32_t)src_pos >> LERP_INTERP_M;
            const int16_t  v0   = row_data->values[idx];
            const int16_t  v1   = row_data->values[idx + 1];
            const int32_t  frac = src_pos - ((int32_t)idx << LERP_INTERP_M);
            const uint8_t  v    = (uint8_t)(v0 + (((v1 - v0) * frac) >> LERP_INTERP_M));

            buf[(WIDTH - 1 - i) * stride + col] = wf_palette[v] | 0xFF000000;
        }

        /* DDA step: additions and comparisons only, zero division */
        accum   += step_r;
        src_pos += step_q;
        if (accum >= DENOM) {
            accum -= DENOM;
            src_pos++;
        }
    }
}

static void waterfall_render_rotated(uint32_t *buf, int stride) {
    uint32_t bandwidth = width_hz;

    if (params.waterfall_zoom.x) {
        bandwidth /= zoom;
    }

    // circular history oldest->newest; newest (last_row_id) -> column 0 (logical top)
    for (uint16_t src_y = 0; src_y < s_wf_w; src_y++) {
        int col = (int)(last_row_id - src_y + s_wf_w) % s_wf_w;
        lerp_row_to_col(&wf_rows[src_y], wf_center_freq, bandwidth, buf, stride, col);
    }
}

/* Called from the main loop (between lv_timer_handler() and drm_flip()) when the
 * direct-render path is enabled. Returns true if a frame was produced. */
bool waterfall_process(void) {
    bool data = __atomic_exchange_n(&s_data_ready, 0, __ATOMIC_ACQUIRE);
    bool cond = __atomic_exchange_n(&s_cond_dirty, 0, __ATOMIC_ACQUIRE);
    if (!data && !cond) {
        return false;
    }

    drm_direct_ctx_t ctx;
    if (!drm_primary_begin_direct(&ctx, (uint32_t)s_wf_w * WIDTH)) {
        return false;
    }

    waterfall_render_rotated((uint32_t *)ctx.buf, s_wf_w);

    lv_area_t area = { .x1 = s_wf_x, .y1 = 0,
                       .x2 = s_wf_x + s_wf_w - 1,
                       .y2 = WIDTH - 1 };
    drm_primary_end_direct(&area);
    return true;
}

static void middle_line_cb(lv_event_t * event) {
    if (params.waterfall_center_line.x && lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!params.waterfall_center_line.x && !lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom = subject_i_get((SubjectInt*)subj);
    update_middle_line();
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_if_shift_changed(Subject *subj, void *user_data) {
    delay = 2;
    if_shift = subject_i_get((SubjectInt*)subj);
    radio_center_freq = cparam_i_get(cfg_fg_freq) - if_shift;
    update_middle_line();
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    delay = 2;
    radio_center_freq = subject_i_get((SubjectInt*)subj) - if_shift;
    wf_center_freq = radio_center_freq;
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_mode_lo_offset_change(Subject *subj, void *user_data) {
    mode_lo_offset = subject_i_get((SubjectInt*)subj);
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void update_middle_line() {
    lv_coord_t width = zoom / 2 + 2;
    lv_style_value_t width_default;
    lv_style_get_prop(&style_waterfall_middle_line, LV_STYLE_LINE_WIDTH, &width_default);
    width = LV_MAX(width, width_default.num);

    lv_coord_t center = if_shift * zoom * WIDTH / width_hz + WIDTH / 2;
    middle_line_points[0].x = center;
    middle_line_points[1].x = center;
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_set_style_line_width(middle_line, width, LV_PART_MAIN);
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
