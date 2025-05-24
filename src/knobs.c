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

static lv_obj_t *obj_label_vol_knob = NULL;
static lv_obj_t *obj_label_mfk_knob = NULL;

lv_obj_t * knobs_init(lv_obj_t * parent) {
    // Basic pos. calc.
    uint16_t y = 480 - BTN_HEIGHT;
    uint16_t x = 0;

    // Set global style
    lv_style_set_width(&knobs_style, 400);
    lv_style_set_height(&knobs_style, KNOBS_HEIGHT);

    // Volume knob label
    obj_label_vol_knob = lv_label_create(parent);
    lv_obj_add_style(obj_label_vol_knob, &knobs_style, 0);
    lv_obj_set_style_text_align(obj_label_vol_knob, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(obj_label_vol_knob, &sony_30, 0);
    lv_obj_set_pos(obj_label_vol_knob, x, y - KNOBS_HEIGHT * 2);

    // MFK knob label
    obj_label_mfk_knob = lv_label_create(parent);
    lv_obj_add_style(obj_label_mfk_knob, &knobs_style, 0);
    lv_obj_set_style_text_align(obj_label_mfk_knob, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(obj_label_mfk_knob, &sony_30, 0);
    lv_obj_set_pos(obj_label_mfk_knob, x, y - KNOBS_HEIGHT * 1);
}

// Yes i know it can be deduplicated but i don't care

void knobs_update_vol(const char * fmt, ...) {
    va_list args;
    static char buf[256];
    static char buf2[256];

    // Skip if object is still NULL
    if(obj_label_vol_knob == NULL) return;

    // Format message
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Generate final string
    snprintf(buf2, sizeof(buf2), "VOL -> %s", buf);
    
    // Update label
    lv_label_set_text(obj_label_vol_knob, buf2);
}

void knobs_update_mfk(const char * fmt, ...) {
    va_list args;
    static char buf[256];
    static char buf2[256];

    // Skip if object is still NULL
    if(obj_label_mfk_knob == NULL) return;

    // Format message
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Generate final string
    snprintf(buf2, sizeof(buf2), "MFK -> %s", buf);
    
    // Update label
    lv_label_set_text(obj_label_mfk_knob, buf2);
}