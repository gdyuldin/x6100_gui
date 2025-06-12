/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 */


#include "knobs.h"

#include "styles.h"
#include "buttons.h"

#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *obj_label_vol_knob_static = NULL;
static lv_obj_t *obj_label_vol_knob_dynamic = NULL;
static lv_obj_t *obj_label_mfk_knob_static = NULL;
static lv_obj_t *obj_label_mfk_knob_dynamic = NULL;

lv_obj_t * knobs_init(lv_obj_t * parent) {
    // Basic positon calculation
    uint16_t y = 480 - BTN_HEIGHT;
    uint16_t x_static = KNOBS_PADDING;
    uint16_t x_dynamic = x_static  + KNOBS_STATIC_WIDTH + KNOBS_PADDING;

    // Init
    lv_font_t *font = &sony_24;
    lv_style_set_height(&knobs_style, KNOBS_HEIGHT);
    LV_IMG_DECLARE(up_icon)
    LV_IMG_DECLARE(down_icon)

    // Static labels (those where labels but now are images)
    // Volume knob label
    obj_label_vol_knob_static = lv_img_create(parent);
    lv_img_set_src(obj_label_vol_knob_static, &up_icon);
    lv_obj_set_size(obj_label_vol_knob_static, KNOBS_STATIC_WIDTH, KNOBS_HEIGHT);
    lv_obj_set_pos(obj_label_vol_knob_static, x_static, y - KNOBS_HEIGHT * 2);

    // MFK knob label
    obj_label_mfk_knob_static = lv_img_create(parent);
    lv_img_set_src(obj_label_mfk_knob_static, &down_icon);
    lv_obj_set_size(obj_label_mfk_knob_static, KNOBS_STATIC_WIDTH, KNOBS_HEIGHT);
    lv_obj_set_pos(obj_label_mfk_knob_static, x_static, y - KNOBS_HEIGHT * 1);

    // Dynamic labels
    // Volume knob label
    obj_label_vol_knob_dynamic = lv_label_create(parent);
    lv_obj_add_style(obj_label_vol_knob_dynamic, &knobs_style, 0);
    lv_obj_set_style_text_align(obj_label_vol_knob_dynamic, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(obj_label_vol_knob_dynamic, font, 0);
    lv_obj_set_width(obj_label_vol_knob_dynamic, KNOBS_DYNAMIC_WIDTH);
    lv_obj_set_pos(obj_label_vol_knob_dynamic, x_dynamic, y - KNOBS_HEIGHT * 2);

    // MFK knob label
    obj_label_mfk_knob_dynamic = lv_label_create(parent);
    lv_obj_add_style(obj_label_mfk_knob_dynamic, &knobs_style, 0);
    lv_obj_set_style_text_align(obj_label_mfk_knob_dynamic, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(obj_label_mfk_knob_dynamic, font, 0);
    lv_obj_set_width(obj_label_mfk_knob_dynamic, KNOBS_DYNAMIC_WIDTH);
    lv_obj_set_pos(obj_label_mfk_knob_dynamic, x_dynamic, y - KNOBS_HEIGHT * 1);
}

void knobs_update_genetic(lv_obj_t * obj, const char * fmt, va_list args) {
    static char buf[256];

    // Skip if object is still NULL
    if(obj == NULL) return;

    // Format message
    vsnprintf(buf, sizeof(buf), fmt, args);
    lv_label_set_text(obj, buf);
}

void knobs_update_vol(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    knobs_update_genetic(obj_label_vol_knob_dynamic, fmt, args);
    va_end(args);
}

void knobs_update_mfk(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    knobs_update_genetic(obj_label_mfk_knob_dynamic, fmt, args);
    va_end(args);
}