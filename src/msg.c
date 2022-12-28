/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022 Belousov Oleg aka R1CBU
 */

#include "msg.h"
#include "styles.h"

static lv_obj_t     *obj;
static char         buf[512];
static lv_timer_t   *timer;

static void msg_timer(lv_timer_t *timer) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t * msg_init(lv_obj_t *parent) {
    obj = lv_label_create(parent);

    lv_obj_add_style(obj, &msg_style, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

    timer = lv_timer_create_basic();
    lv_timer_set_cb(timer, msg_timer);

    return obj;
}


void msg_set_text_fmt(const char * fmt, ...) {
    va_list args;

    va_start(args, fmt);
    lv_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_label_set_text(obj, buf);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    
    lv_timer_reset(timer);
    lv_timer_set_period(timer, 1000);
}