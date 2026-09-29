/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "waterfall.h"

#include "globals.h"
#include "styles.h"
#include "radio.h"
#include "events.h"
#include "cfg/cfg_api.h"
#include "band_info.h"
#include "meter.h"
#include "display.h"
#include "dsp.h"
#include "util.h"
#include "pubsub_ids.h"

#include "lv_drivers/display/drm.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#if LV_DRAW_NEON && LV_COLOR_DEPTH == 32
#include <arm_neon.h>
#endif

#define WIDTH SCREEN_WIDTH

/* Rows are preallocated once for the largest possible strip so that resizing
 * never reallocates the ring while the DSP thread writes into it. */
#define WATERFALL_MAX_HEIGHT SCREEN_HEIGHT

typedef struct {
    uint8_t values[WATERFALL_NFFT];
    uint32_t center_freq;
    uint32_t width;
} wf_data_row_t;

static lv_obj_t         *obj;
static bool             ready = false;

static int32_t          width_hz = 100000;

static uint8_t          delay = 0;

static wf_data_row_t    *wf_rows;
static uint16_t         last_row_id;

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

static void on_zoom_changed(Subject *subj, void *user_data);
static void update_freq_cb(Subject *subj, void *user_data);
static void on_mode_lo_offset_change(Subject *subj, void *user_data);
// static void on_if_shift_changed(Subject *subj, void *user_data);
static void on_dialog_start_cb(void *s, lv_msg_t *m);
static void on_dialog_stop_cb(void *s, lv_msg_t *m);

static uint32_t waterfall_sub_id = DSP_FRAME_SUB_INVALID;

static void waterfall_frame_cb(const dsp_frame_t *frame, void *user_data) {
    (void)user_data;
    waterfall_data(frame->psd_db, frame->size, frame->tx, frame->base_freq, frame->width_hz, frame->min, frame->max);
}

lv_obj_t * waterfall_init(lv_obj_t * overlay_parent, lv_coord_t y, lv_coord_t h) {
    s_wf_x = y;
    s_wf_w = h;

    wf_rows = calloc(WATERFALL_MAX_HEIGHT, sizeof(*wf_rows));
    for (size_t i = 0; i < WATERFALL_MAX_HEIGHT; i++) {
        wf_rows[i].center_freq = wf_center_freq;
        memset(wf_rows[i].values, 0, WATERFALL_NFFT);
    }
    last_row_id = 0;

    obj = lv_obj_create(overlay_parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_pos(obj, 0, y);
    lv_obj_set_size(obj, WIDTH, h);

    band_info_init(obj);

    ready = true;

    subject_subscribe((Subject*)cfg.cur.fg_freq(), update_freq_cb, NULL);
    subject_subscribe_and_notify((Subject*)cfg.band.if_shift(), update_freq_cb, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg.mode.zoom(), on_zoom_changed, NULL);
    subject_subscribe_and_notify((Subject*)cfg.cur.mode_lo_offset(), on_mode_lo_offset_change, NULL);

    lv_msg_subscribe(MSG_DIALOG_START, on_dialog_start_cb, NULL);
    lv_msg_subscribe(MSG_DIALOG_STOP, on_dialog_stop_cb, NULL);

    if (waterfall_sub_id == DSP_FRAME_SUB_INVALID) {
        const dsp_frame_cfg_t sub_cfg = {
            .nfft             = WATERFALL_NFFT,
            .chunks_per_frame = DSP_FRAME_DEFAULT_CHUNKS,
            .allow_vary_freq  = false,
        };
        waterfall_sub_id = dsp_frame_subscribe(&sub_cfg, waterfall_frame_cb, NULL);
    }

    return obj;
}

void waterfall_set_geometry(lv_coord_t y, lv_coord_t h) {
    if (h < 1 || h > WATERFALL_MAX_HEIGHT) {
        return;
    }

    s_wf_x = y;
    s_wf_w = h;

    lv_obj_set_pos(obj, 0, y);
    lv_obj_set_size(obj, WIDTH, h);

    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

void waterfall_set_enabled(bool enabled) {
    dsp_frame_set_active(waterfall_sub_id, enabled);
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % WATERFALL_MAX_HEIGHT;
}

void waterfall_data(const float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint32_t width_hz, float min, float max) {
    if (!ready) {
        return;
    }
    if (delay && (base_freq == 0))
    {
        delay--;
        return;
    }
    scroll_down();

    if (base_freq == 0) {
        base_freq = wf_center_freq + mode_lo_offset;
    } else if (tx) {
        // New patched firmware
        base_freq += mode_lo_offset;
    }
    wf_rows[last_row_id].center_freq = base_freq;
    wf_rows[last_row_id].width = width_hz;

    const float scale = 255.0f / (max - min);

    for (uint16_t x = 0; x < size; x++) {
        float   v = (data_buf[x] - min) * scale;
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

            buf[(WIDTH - 1 - i) * stride + col] = style.wf_palette[v] | 0xFF000000;
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

static inline uint32_t add_px(uint32_t bg, uint8_t fr, uint8_t fg, uint8_t fb) {
    uint32_t r = ((bg >> 16) & 0xFF) + fr;
    r = r > 255 ? 255 : r;
    uint32_t g = ((bg >> 8) & 0xFF) + fg;
    g = g > 255 ? 255 : g;
    uint32_t b = ((bg >> 0) & 0xFF) + fb;
    b = b > 255 ? 255 : b;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void draw_additive_row(uint32_t *line, uint32_t n, uint8_t fr, uint8_t fg, uint8_t fb) {
#if LV_DRAW_NEON && LV_COLOR_DEPTH == 32
    const uint8x16_t fg_v = {
        fb, fg, fr, 0, fb, fg, fr, 0, fb, fg, fr, 0, fb, fg, fr, 0,
    };
    uint8_t *p = (uint8_t *)line;
    uint8_t *end = p + (size_t)n * 4;
    for (; p + 16 <= end; p += 16) {
        uint8x16_t bg = vld1q_u8(p);
        vst1q_u8(p, vqaddq_u8(bg, fg_v));
    }
    line = (uint32_t *)p;
    n = (uint32_t)(end - p) / 4;
#endif
    for (uint32_t i = 0; i < n; i++) {
        line[i] = add_px(line[i], fr, fg, fb);
    }
}

static void waterfall_render_rotated(uint32_t *buf, int stride) {
    uint32_t bandwidth = width_hz / zoom;

    // circular history oldest->newest; newest (last_row_id) -> column 0 (logical top)
    for (uint16_t col = 0; col < s_wf_w; col++) {
        int row = (int)(last_row_id - col + WATERFALL_MAX_HEIGHT) % WATERFALL_MAX_HEIGHT;
        lerp_row_to_col(&wf_rows[row], wf_center_freq, bandwidth, buf, stride, col);
    }

    lv_style_value_t style_val;
    lv_style_get_prop(&style.waterfall_middle_line, LV_STYLE_LINE_COLOR, &style_val);
    lv_color_t line_color = style_val.color;
    lv_style_get_prop(&style.waterfall_middle_line, LV_STYLE_LINE_WIDTH, &style_val);
    lv_coord_t style_width = style_val.num;
    lv_style_get_prop(&style.waterfall_middle_line, LV_STYLE_LINE_OPA, &style_val);
    lv_opa_t line_opa = (lv_opa_t)style_val.num;

    bool line_visible = param_i_get(cfg.ui.waterfall_center_line());
    lv_coord_t line_width = LV_MAX(zoom / 2 + 2, style_width);

    if (line_visible && line_opa > LV_OPA_MIN) {
        int32_t center_f = if_shift * zoom * WIDTH / width_hz + (WIDTH + line_width) / 2;

        /* Pre-scale foreground once (constant color + opacity). */
        uint8_t fr = (uint8_t)((line_color.ch.red * line_opa + 255u) >> 8);
        uint8_t fg = (uint8_t)((line_color.ch.green * line_opa + 255u) >> 8);
        uint8_t fb = (uint8_t)((line_color.ch.blue * line_opa + 255u) >> 8);

        for (lv_coord_t w = 0; w < line_width; w++) {
            int32_t freq_col = (int32_t)center_f - w;
            if (freq_col < 0 || freq_col >= WIDTH) continue;
            int32_t row = WIDTH - 1 - freq_col;
            draw_additive_row(&buf[row * stride], (uint32_t)s_wf_w, fr, fg, fb);
        }
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

static void update_freq_cb(Subject *subj, void *user_data) {
    delay = 2;
    if_shift = param_i_get(cfg.band.if_shift());
    wf_center_freq = cparam_i_get(cfg.cur.fg_freq()) - if_shift;
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom = subject_i_get((SubjectInt*)subj);
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_mode_lo_offset_change(Subject *subj, void *user_data) {
    mode_lo_offset = subject_i_get((SubjectInt*)subj);
    __atomic_store_n(&s_cond_dirty, 1, __ATOMIC_RELEASE);
}

static void on_dialog_start_cb(void *s, lv_msg_t *m) {
    refresh_period = 2;
}

static void on_dialog_stop_cb(void *s, lv_msg_t *m) {
    refresh_period = 1;
}
