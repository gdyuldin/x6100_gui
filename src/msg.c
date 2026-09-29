/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "msg.h"

#include "styles.h"
#include "util.h"
#include "events.h"

#include <stdio.h>
#include <stdlib.h>

#define FADE_TIME 250
#define DURATION 2000
#define DURATION_LONG 4000

static lv_obj_t     *container;
static lv_obj_t     *align_base;
static lv_obj_t     *obj;
static lv_timer_t   *fade_out_timer=NULL;
static lv_anim_t    fade;
static bool         fade_run = false;
static uint32_t     timer_end=0;

enum msg_type_t {
    MSG_UPDATE,
    MSG_SCHEDULE,
};

typedef struct {
    char            text[128];
    enum msg_type_t type;
    uint16_t        dur;
} delayed_message_t;

static void fade_out_timer_cb(lv_timer_t *t) {
    lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_TRANSP);
    lv_anim_start(&fade);
    fade_run = true;
    fade_out_timer = NULL;
}

static void fade_anim(void * obj, int32_t v) {
    lv_obj_set_style_opa(obj, v, 0);
}

static void fade_ready(lv_anim_t * a) {
    fade_run = false;
}

static void msg_show_timer(lv_timer_t *t) {
    delayed_message_t * msg = t->user_data;
    t->user_data = NULL;
    if (fade_out_timer != NULL) {
        lv_timer_del(fade_out_timer);
    }
    lv_label_set_text(obj, msg->text);
    lv_obj_move_foreground(container);
    lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_COVER);
    fade_run = true;
    lv_anim_start(&fade);
    fade_out_timer = lv_timer_create(fade_out_timer_cb, msg->dur - FADE_TIME, NULL);
    lv_timer_set_repeat_count(fade_out_timer, 1);
    free(msg);
}

static void msg_update_cb(lv_event_t * e) {
    delayed_message_t *msg = (delayed_message_t *) lv_event_get_param(e);
    uint32_t delay;
    uint32_t cur_tick = lv_tick_get();
    if (timer_end < cur_tick) {
        timer_end = cur_tick;
    }
    if (msg->type == MSG_UPDATE) {
        delay = 0;
        timer_end = cur_tick;
    } else {
        delay = timer_end - cur_tick;
    }
    delayed_message_t *msg_copy = malloc(sizeof(delayed_message_t));
    *msg_copy = *msg;
    lv_timer_t *timer = lv_timer_create(msg_show_timer, delay, (void *)(msg_copy));
    lv_timer_set_repeat_count(timer, 1);
    timer_end += msg->dur;
}

static void create_msg(const char * fmt, enum msg_type_t type, uint16_t dur, va_list args) {
    delayed_message_t * msg = (delayed_message_t *) malloc(sizeof(delayed_message_t));
    vsnprintf(msg->text, sizeof(msg->text), fmt, args);
    msg->type = type;
    msg->dur = dur;
    event_send(obj, EVENT_MSG_UPDATE, (void*)msg);
}

void msg_align(void) {
    if (!container || !align_base) {
        return;
    }
    lv_obj_update_layout(container);
    lv_obj_align_to(container, align_base, LV_ALIGN_CENTER, 0, -BTN_HEIGHT / 2);
}

lv_obj_t * msg_init(lv_obj_t *base) {
    align_base = base;

    container = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(container);
    lv_obj_add_style(container, &style.msg, 0);
    msg_align();

    obj = lv_label_create(container);
    lv_obj_set_width(obj, lv_obj_get_width(container) - 20);
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_center(obj);

    lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL);
    lv_obj_add_event_cb(obj, msg_update_cb, EVENT_MSG_UPDATE, NULL);
    lv_label_set_recolor(obj, true);

    lv_anim_init(&fade);
    lv_anim_set_var(&fade, container);
    lv_anim_set_time(&fade, FADE_TIME);
    lv_anim_set_exec_cb(&fade, fade_anim);
    lv_anim_set_ready_cb(&fade, fade_ready);

    return obj;
}

void msg_update_text_fmt(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    create_msg(fmt, MSG_UPDATE, DURATION, args);
    va_end(args);
}

void msg_schedule_text_fmt(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    create_msg(fmt, MSG_SCHEDULE, DURATION, args);
    va_end(args);
}

void msg_schedule_long_text_fmt(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    create_msg(fmt, MSG_SCHEDULE, DURATION_LONG, args);
    va_end(args);
}
