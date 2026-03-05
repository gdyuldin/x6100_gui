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
#include "band_info.h"
#include "meter.h"
#include "backlight.h"
#include "dsp.h"
#include "util.h"
#include "pubsub_ids.h"
#include "scheduler.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define PX_BYTES    sizeof(lv_color_t)
#define DEFAULT_MIN S4
#define DEFAULT_MAX S9_20
#define WIDTH 800

typedef struct {
    uint8_t values[WATERFALL_NFFT];
    uint32_t center_freq;
    uint32_t width;
} wf_data_row_t;

static lv_obj_t         *obj;
static lv_obj_t         *img;
static bool             ready = false;

static lv_style_t       middle_line_style;
static lv_obj_t         *middle_line;
static lv_point_t       middle_line_points[] = { {0, 0}, {0, 0} };

static lv_coord_t       height;
static int32_t          width_hz = 100000;

static float            grid_min = DEFAULT_MIN;
static float            grid_max = DEFAULT_MAX;

static lv_img_dsc_t     *frame;
static uint8_t          delay = 0;

static wf_data_row_t    *wf_rows;
static uint16_t         last_row_id;

static int32_t          radio_center_freq = 0;
static int32_t          wf_center_freq = 0;
static int32_t          lo_offset = 0;
static int32_t          if_shift = 0;

static uint8_t          refresh_period = 1;
static uint8_t          refresh_counter = 0;

static uint8_t          zoom = 1;

static void refresh_waterfall( void * arg);
static void draw_middle_line();
static void update_middle_line();
static void redraw_cb(lv_event_t * e);
static void on_zoom_changed(Subject *subj, void *user_data);
static void on_fg_freq_change(Subject *subj, void *user_data);
static void on_lo_offset_change(Subject *subj, void *user_data);
static void on_if_shift_changed(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);


lv_obj_t * waterfall_init(lv_obj_t * parent) {
    subject_add_observer_and_call(cfg_cur.fg_freq, on_fg_freq_change, NULL);
    wf_center_freq = radio_center_freq;

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &waterfall_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // Middle line style
    lv_style_init(&middle_line_style);
    lv_style_set_line_width(&middle_line_style, 1);
    lv_style_set_line_color(&middle_line_style, lv_color_hex(0xAAAAAA));
    lv_style_set_line_opa(&middle_line_style, LV_OPA_60);
    lv_style_set_blend_mode(&middle_line_style, LV_BLEND_MODE_ADDITIVE);
    lv_style_set_pad_all(&middle_line_style, 0);

    subject_add_delayed_observer(cfg_cur.zoom, on_zoom_changed, NULL);
    subject_add_delayed_observer(cfg_cur.band->if_shift.val, on_if_shift_changed, NULL);

    subject_add_observer_and_call(cfg_cur.lo_offset, on_lo_offset_change, NULL);
    subject_add_observer(cfg.auto_level_enabled.val, on_grid_min_change, NULL);
    subject_add_observer_and_call(cfg_cur.band->grid.min.val, on_grid_min_change, NULL);
    subject_add_observer(cfg.auto_level_enabled.val, on_grid_max_change, NULL);
    subject_add_observer_and_call(cfg_cur.band->grid.max.val, on_grid_max_change, NULL);

    return obj;
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % height;
}

void waterfall_data(float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint32_t width_hz) {
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
        base_freq = radio_center_freq + lo_offset;
    } else if (tx) {
        // New patched firmware
        base_freq += lo_offset;
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
    scheduler_put_noargs(refresh_waterfall);
}

static void do_scroll_cb(lv_event_t * event) {
    if (wf_center_freq == radio_center_freq) {
        return;
    }
    if (params.waterfall_smooth_scroll.x) {
        wf_center_freq += (radio_center_freq - wf_center_freq) / 10 + 1;
    } else {
        wf_center_freq = radio_center_freq;
    }
    scheduler_put_noargs(refresh_waterfall);
}

void waterfall_set_height(lv_coord_t h) {
    lv_obj_set_height(obj, h);
    lv_obj_update_layout(obj);

    /* For more accurate horizontal scroll, it should be a "multiple of 500Hz" */
    /* 800 * 500Hz / 100000Hz = 4.0px */

    height = lv_obj_get_height(obj);

    frame = lv_img_buf_alloc(WIDTH, height, LV_IMG_CF_TRUE_COLOR);

    img = lv_img_create(obj);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(img, frame);

    wf_rows = calloc(height, sizeof(*wf_rows));
    for (size_t i = 0; i < height; i++) {
        wf_rows[i].center_freq = radio_center_freq;
        memset(wf_rows[i].values, 0, WATERFALL_NFFT);
    }
    last_row_id = 0;

    lv_obj_add_event_cb(img, do_scroll_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    waterfall_min_max_reset();
    band_info_init(obj);
    draw_middle_line();
    on_zoom_changed(cfg_cur.zoom, NULL);
    on_if_shift_changed(cfg_cur.band->if_shift.val, NULL);
    ready = true;
}

static void middle_line_cb(lv_event_t * event) {
    if (params.waterfall_center_line.x && lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(middle_line, 600, 0);
        return;
    }
    if (!params.waterfall_center_line.x && !lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
}

static void draw_middle_line() {
    middle_line_points[1].y = height;
    middle_line = lv_line_create(obj);
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_add_style(middle_line, &middle_line_style, 0);
    lv_obj_center(middle_line);
    lv_obj_add_event_cb(obj, middle_line_cb, LV_EVENT_DRAW_POST_END, NULL);
}

void waterfall_min_max_reset() {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        grid_min = DEFAULT_MIN;
        grid_max = DEFAULT_MAX;
    } else {
        grid_min = subject_get_int(cfg_cur.band->grid.min.val);
        grid_max = subject_get_int(cfg_cur.band->grid.max.val);
    }
}

void waterfall_update_max(float db) {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        grid_max = db - subject_get_float(cfg.auto_level_offset.val);
    }
}

void waterfall_update_min(float db) {
    if (subject_get_int(cfg.auto_level_enabled.val)) {
        grid_min = db - subject_get_float(cfg.auto_level_offset.val);
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


#define LERP_INTERP_M 3
#define LERP_INTERP_FRAC (1 << LERP_INTERP_M)
static inline void lerp_row(wf_data_row_t *row_data, uint32_t dst_center_freq, uint32_t dst_width_hz, uint8_t *dst) {
    // Skip empty rows at start
    if (!row_data->width) {
        memset(dst, 0, WIDTH);
        return;
    }
    uint32_t src_start = row_data->center_freq - row_data->width / 2;
    uint32_t src_end = src_start + row_data->width;

    uint32_t dst_start = dst_center_freq - dst_width_hz / 2;
    uint32_t dst_end = dst_start + dst_width_hz;

    if ((src_start > dst_end) || (src_end < dst_start)) {
        // No overlap
        memset(dst, 0, WIDTH);
        return;
    }

    uint32_t dst_half_width_hz = dst_width_hz / 2;
    for (size_t i = 0; i < WIDTH; i++) {
        int32_t freq = dst_center_freq + (dst_width_hz * i + dst_half_width_hz) / WIDTH - dst_half_width_hz;
        // src pos is multiplied by 8
        int32_t src_pos = (freq - src_start) * (WATERFALL_NFFT << LERP_INTERP_M) / row_data->width;
        if (src_pos < 0) {
            dst[i] = 0;
            continue;
        }
        uint32_t src_pos_int = (uint32_t)src_pos >> LERP_INTERP_M;
        if (src_pos_int >= (WATERFALL_NFFT - 2)) {
            dst[i] = 0;
        } else {
            int16_t v0 = row_data->values[src_pos_int];
            int16_t v1 = row_data->values[src_pos_int + 1];
            v0 += ((v1 - v0) * (src_pos - ((uint32_t)src_pos_int << LERP_INTERP_M))) >> LERP_INTERP_M;
            dst[i] = v0;
        }
    }
}

static void redraw_cb(lv_event_t * e) {
    int32_t src_x_offset;
    uint16_t src_y, src_x0, dst_y, dst_x;

    uint8_t current_zoom = 1;
    uint32_t bandwidth = width_hz;
    if (params.waterfall_zoom.x) {
        current_zoom = zoom;
        bandwidth /= zoom;
    }
    lv_color_t black = lv_color_black();
    lv_color_t px_color;
    uint8_t dst[WIDTH];

    for (src_y = 0; src_y < height; src_y++) {
        wf_data_row_t row_data = wf_rows[src_y];
        dst_y = ((height - src_y + last_row_id) % height);

        lerp_row(&row_data, wf_center_freq, bandwidth, dst);
        for (size_t i = 0; i < WIDTH; i++) {
            *((lv_color_t*)frame->data + (dst_y * WIDTH + i)) = (lv_color_t)wf_palette[dst[i]];
        }
    }
}

static void refresh_waterfall( void * arg) {
    if (!ready) {
        return;
    }
    refresh_counter++;
    if (refresh_counter >= refresh_period) {
        refresh_counter = 0;
        redraw_cb(NULL);
        lv_obj_invalidate(img);
    }
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom = subject_get_int(subj);
    update_middle_line();
}

static void on_if_shift_changed(Subject *subj, void *user_data) {
    delay = 2;
    if_shift = subject_get_int(subj);
    radio_center_freq = subject_get_int(cfg_cur.fg_freq) - if_shift;
    update_middle_line();
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    delay = 2;
    radio_center_freq = subject_get_int(subj) - if_shift;
}

static void on_lo_offset_change(Subject *subj, void *user_data) {
    lo_offset = subject_get_int(subj);
}

static void update_middle_line() {
    lv_coord_t width = zoom / 2 + 2;
    lv_obj_set_pos(middle_line, if_shift * zoom * WIDTH / width_hz + width / 2, 0);
    lv_style_set_line_width(&middle_line_style, width);
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
