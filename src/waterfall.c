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

#define PX_BYTES    4

float                   waterfall_auto_min;
float                   waterfall_auto_max;

static lv_obj_t         *obj;
static lv_obj_t         *img;

static lv_coord_t       width;
static lv_coord_t       height;
static int32_t          width_hz = 100000;
static uint32_t         line_len;

static int              grid_min = -70;
static int              grid_max = -40;

static lv_img_dsc_t     *frame;
static lv_color_t       palette[256];
static int16_t          scroll_hor = 0;
static int16_t          scroll_hor_surplus = 0;
static uint8_t          delay = 0;

lv_obj_t * waterfall_init(lv_obj_t * parent) {
    obj = lv_obj_create(parent);
    
    lv_obj_add_style(obj, &waterfall_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

static void scroll_down() {
    memmove(frame->data + line_len, frame->data, frame->data_size - line_len);
}

static void scroll_right(int16_t px) {
    uint8_t     *ptr = frame->data;
    uint16_t    offset = px * PX_BYTES;
    uint16_t    tail = (width - px) * PX_BYTES;
    
    for (int y = 0; y < height; y++) {
        memmove(ptr, ptr + offset, tail);
        memset(ptr + tail, 0, offset);
        
        ptr += line_len;
    }
}

static void scroll_left(int16_t px) {
    uint8_t     *ptr = frame->data;
    uint16_t    offset = px * PX_BYTES;
    uint16_t    tail = (width - px) * PX_BYTES;
    
    for (int y = 0; y < height; y++) {
        memmove(ptr + offset, ptr, tail);
        memset(ptr, 0, offset);
        
        ptr += line_len;
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
        
        lv_img_buf_set_px_color(frame, width - x, 0, palette[id]);
    }
    
    event_send(img, LV_EVENT_REFRESH, NULL);
}

static void do_scroll_cb(lv_event_t * event) {
    int16_t px;
    if (scroll_hor == 0) {
        return;
    }

    if (params.waterfall_smooth_scroll.x) {
        px = (abs(scroll_hor) / 10) + 1;
    } else {
        px = abs(scroll_hor);
    }

    if (scroll_hor > 0) {
        scroll_right(px);
        scroll_hor -= px;
    } else {
        scroll_left(px);
        scroll_hor += px;
    }
    
    event_send(img, LV_EVENT_REFRESH, NULL);
}

void waterfall_set_height(lv_coord_t h) {
    lv_obj_set_height(obj, h);
    lv_obj_update_layout(obj);

    /* For more accurate horizontal scroll, it should be a "multiple of 500Hz" */
    /* 800 * 500Hz / 100000Hz = 4.0px */
    
    width = 800;
    height = lv_obj_get_height(obj);

    frame = lv_img_buf_alloc(width, height, LV_IMG_CF_TRUE_COLOR);

    line_len = frame->data_size / frame->header.h;
    
    styles_waterfall_palette(palette, 256);

    img = lv_img_create(obj);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(img, frame);
    lv_obj_add_event_cb(img, do_scroll_cb, LV_EVENT_DRAW_POST, NULL);
    
    waterfall_band_set();
    band_info_init(obj);
}

void waterfall_clear() {
    memset(frame->data,0, frame->data_size);
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

void waterfall_change_freq(int16_t df) {
    delay = 2;
    uint16_t    hz_per_pixel = width_hz / width;

    df += scroll_hor_surplus;
    scroll_hor += df / hz_per_pixel;

    scroll_hor_surplus = df % hz_per_pixel;

    if (scroll_hor) {
        lv_obj_invalidate(img);
    }
}
