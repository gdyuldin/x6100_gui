/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>

#include "waterfall.h"
#include "styles.h"
#include "radio.h"
#include "events.h"
#include "params.h"
#include "bands.h"
#include "band_info.h"
#include "meter.h"
#include "backlight.h"
#include "dsp.h"
#include "util.h"

#define PX_BYTES    4

float                   waterfall_auto_min;
float                   waterfall_auto_max;

static lv_obj_t         *obj;
static lv_obj_t         *img;

static lv_style_t       middle_line_style;
static lv_obj_t         *middle_line;
static lv_point_t       middle_line_points[] = { {0, 0}, {0, 0} };

static lv_coord_t       width;
static lv_coord_t       height;
static int32_t          width_hz = 100000;

static int              grid_min = -70;
static int              grid_max = -40;

static lv_img_dsc_t     *frame;
static lv_color_t       palette[256];
static int16_t          scroll_hor = 0;
static int16_t          scroll_hor_surplus = 0;
static uint8_t          delay = 0;


static int              *x_offsets;
static uint16_t         last_row_id;
static uint8_t          *waterfall_cache;

static uint8_t          *frame_buf;
static size_t           buf_offset;

lv_img_dsc_t* img_2buf_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf);
void draw_middle_line();
void redraw();


lv_obj_t * waterfall_init(lv_obj_t * parent) {
    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &waterfall_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // Middle line style
    lv_style_init(&middle_line_style);
    lv_style_set_line_width(&middle_line_style, 1);
    lv_style_set_line_color(&middle_line_style, lv_color_hex(0xAAAAAA));
    lv_style_set_line_opa(&middle_line_style, LV_OPA_60);

    return obj;
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % height;
}

static void scroll_lr(int16_t px) {
    for (size_t i = 0; i < height; i++) {
        x_offsets[i] -= px;
    }
}

void waterfall_data(float *data_buf, uint16_t size) {
    if (delay)
    {
        delay--;
        return;
    }

    if (scroll_hor) {
        return;
    }

    scroll_down();

    float min = params.waterfall_auto_min.x ? waterfall_auto_min + 6.0f : grid_min;
    float max = params.waterfall_auto_max.x ? waterfall_auto_max + 3.0f : grid_max;

    for (int x = 0; x < width; x++) {
        uint16_t    index = x * size / width;
        float       v = (data_buf[index] - min) / (max - min);

        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }

        uint8_t id = v * 255;
        lv_memcpy_small(&waterfall_cache[(last_row_id * width + width - 1 - x) * PX_BYTES], &palette[id], PX_BYTES);
        x_offsets[last_row_id] = 0;
    }

    redraw();
}

static void do_scroll_cb(lv_event_t * event) {
    int16_t px;

    if (scroll_hor == 0) {
        return;
    }

    if (params.waterfall_smooth_scroll.x) {
        px = ((abs(scroll_hor) / 10) + 1) * sign(scroll_hor);
    } else {
        px = scroll_hor;
    }
    scroll_lr(px);
    scroll_hor -= px;
    redraw();
}

void waterfall_set_height(lv_coord_t h) {
    lv_obj_set_height(obj, h);
    lv_obj_update_layout(obj);

    /* For more accurate horizontal scroll, it should be a "multiple of 500Hz" */
    /* 800 * 500Hz / 100000Hz = 4.0px */

    width = 800;
    height = lv_obj_get_height(obj);

    frame = img_2buf_alloc(width, height, LV_IMG_CF_TRUE_COLOR);
    frame_buf = frame->data;
    buf_offset = 0;

    styles_waterfall_palette(palette, 256);

    img = lv_img_create(obj);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(img, frame);

    x_offsets = malloc(height * sizeof(int));
    for (size_t i = 0; i < height; i++) {
        x_offsets[i] = 0;
    }
    last_row_id = 0;
    waterfall_cache = malloc(frame->data_size);
    memset(waterfall_cache, 0, frame->data_size);

    lv_obj_add_event_cb(img, do_scroll_cb, LV_EVENT_DRAW_POST_END, NULL);

    waterfall_band_set();
    band_info_init(obj);
    draw_middle_line();
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

void draw_middle_line() {
    middle_line_points[1].y = height;
    middle_line = lv_line_create(obj);
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_add_style(middle_line, &middle_line_style, 0);
    lv_obj_center(middle_line);
    lv_obj_add_event_cb(obj, middle_line_cb, LV_EVENT_DRAW_POST_END, NULL);
}


void waterfall_clear() {
    memset(waterfall_cache, 0, frame->data_size);
    scroll_hor = 0;
    scroll_hor_surplus = 0;
}

void waterfall_band_set() {
    grid_min = params_band.grid_min;
    grid_max = params_band.grid_max;
}

void waterfall_change_max(int16_t d) {
    int16_t x = params_band.grid_max + d;

    if (x > S9_40) {
        x = S9_40;
    } else if (x < S8) {
        x = S8;
    }

    params_lock();
    params_band.grid_max = x;
    params_unlock(&params_band.durty.grid_max);

    grid_max = x;
}

void waterfall_change_min(int16_t d) {
    int16_t x = params_band.grid_min + d;

    if (x > S7) {
        x = S7;
    } else if (x < S_MIN) {
        x = S_MIN;
    }

    params_lock();
    params_band.grid_min = x;
    params_unlock(&params_band.durty.grid_min);

    grid_min = x;
}

void waterfall_change_freq(int64_t df) {
    delay = 2;
    uint16_t    hz_per_pixel = width_hz / width;

    df += scroll_hor_surplus;
    scroll_hor += df / hz_per_pixel;

    scroll_hor_surplus = df % hz_per_pixel;
}

void redraw() {
    int x_offset;
    size_t n, src_pos, dst_pos;

    buf_offset = buf_offset == 0 ? frame->data_size : 0;

    memset(frame_buf + buf_offset, 0, frame->data_size);

    for (size_t i = 0; i < height; i++) {
        x_offset = x_offsets[i];
        if ((x_offset >= width) || (x_offset <= -width)){
            continue;
        }
        if (x_offset >= 0) {
            n = width - x_offset;
            src_pos = 0;
            dst_pos = x_offset;
        } else {
            n = width + x_offset;
            src_pos = -x_offset;
            dst_pos = 0;
        }
        src_pos += i * width;
        dst_pos += ((height - i + last_row_id) % height) * width;

        memcpy(frame_buf + buf_offset + dst_pos * PX_BYTES, waterfall_cache + src_pos * PX_BYTES, n * PX_BYTES);
    }
    frame->data = frame_buf + buf_offset;
    event_send(img, LV_EVENT_REFRESH, NULL);
}


lv_img_dsc_t* img_2buf_alloc(lv_coord_t w, lv_coord_t h, lv_img_cf_t cf) {
    /*Allocate image descriptor*/
    lv_img_dsc_t * dsc = lv_mem_alloc(sizeof(lv_img_dsc_t));
    if(dsc == NULL)
        return NULL;

    lv_memset_00(dsc, sizeof(lv_img_dsc_t));

    /*Get image data size*/
    dsc->data_size = lv_img_buf_get_img_size(w, h, cf);
    if(dsc->data_size == 0) {
        lv_mem_free(dsc);
        return NULL;
    }

    /*Allocate raw buffer*/
    dsc->data = lv_mem_alloc(dsc->data_size * 2);
    if(dsc->data == NULL) {
        lv_mem_free(dsc);
        return NULL;
    }
    lv_memset_00((uint8_t *)dsc->data, dsc->data_size * 2);

    /*Fill in header*/
    dsc->header.always_zero = 0;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = cf;
    return dsc;
}
