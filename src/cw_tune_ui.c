/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "cw_tune_ui.h"

#include "events.h"
#include "styles.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"
#include "scheduler.h"

#include <math.h>

#include <aether_radio/x6100_control/control.h>

#define BLOCK_W 4
#define SPACING 3
#define N_BLOCKS 15
#define WIDTH (N_BLOCKS * (BLOCK_W + SPACING) + SPACING)
#define HEIGHT 30
#define BLOCK_HZ 10

static lv_draw_rect_dsc_t rect_dsc;
static lv_draw_rect_dsc_t rect_active_dsc;
static lv_color_t         active_color;

static lv_color_t color_good;
static lv_color_t color_fair;
static lv_color_t color_bad;

static lv_anim_t fade_anim;
static uint8_t   fade_mix = 0;

static lv_obj_t *obj;

static int8_t cur_freq = -100;

static void draw_cb(lv_event_t * e);
static void update_visibility(Subject *subj, void *user_data);
static void fade_anim_cb(void * var, int32_t val);
static void refresh_anim_cb(void *);

void cw_tune_init(lv_obj_t *parent)
{

    color_good = lv_color_hex(COLOR_LIGHT_GREEN);
    color_fair = lv_color_hex(COLOR_LIGHT_YELLOW);
    color_bad = lv_color_hex(COLOR_LIGHT_RED);

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = style.colors.mark;
    rect_dsc.radius = 5;
    rect_dsc.bg_opa = LV_OPA_50;

    lv_draw_rect_dsc_init(&rect_active_dsc);
    rect_active_dsc.radius = 5;

    obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(obj, HEIGHT);
    lv_obj_set_width(obj, WIDTH);
    lv_obj_add_style(obj, &style.cw_tune, 0);

    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, obj);
    lv_anim_set_exec_cb(&fade_anim, fade_anim_cb);
    lv_anim_set_values(&fade_anim, 255, 0);
    lv_anim_set_time(&fade_anim, 500);
    lv_anim_set_delay(&fade_anim, 1000);

    lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    subject_subscribe_delayed((Subject*)cfg.cur.mode(), update_visibility, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg.cw.tune(), update_visibility, NULL);
}

void cw_tune_set_freq(float hz) {
    int8_t new_id = roundf(hz / BLOCK_HZ);
    if (LV_ABS(new_id) <= 1) {
        active_color = color_good;
    } else if (LV_ABS(new_id) <= 2) {
        active_color = color_fair;
    } else {
        active_color = color_bad;
    }
    new_id += N_BLOCKS / 2;
    if (new_id < 0) new_id = 0;
    if (new_id > N_BLOCKS - 1) new_id = N_BLOCKS - 1;

    if (cur_freq != new_id){
        cur_freq = new_id;
        event_send(obj, LV_EVENT_REFRESH, NULL);
    }
    fade_mix = 255;
    scheduler_put_noargs(refresh_anim_cb);
}

static void draw_cb(lv_event_t * e) {
    int16_t x, h;
    int16_t y_b=HEIGHT - 1;
    int16_t w=BLOCK_W;

    lv_draw_ctx_t * ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t * dsc;
    lv_area_t coords, offset;

    lv_obj_get_coords(obj, &offset);
    rect_active_dsc.bg_color = lv_color_mix(active_color, rect_dsc.bg_color, fade_mix);
    rect_active_dsc.bg_opa = rect_dsc.bg_opa + (uint16_t)fade_mix * (LV_OPA_70 - rect_dsc.bg_opa) / 255;

    for (int16_t i = 0; i < N_BLOCKS; i++) {
        x = SPACING + i * (BLOCK_W + SPACING);
        h = (HEIGHT - 3) * 2 / (LV_ABS(i - N_BLOCKS / 2) + 2);
        if (cur_freq == i) {
            dsc = &rect_active_dsc;
        } else {
            dsc = &rect_dsc;
        }
        coords.x1 = x;
        coords.y1 = y_b - h;
        coords.x2 = coords.x1 + w;
        coords.y2 = coords.y1 + h;
        lv_area_move(&coords, offset.x1, offset.y1);
        lv_draw_rect(ctx, dsc, &coords);
    }
}

static void update_visibility(Subject *subj, void *user_data) {
    x6100_mode_t mode = cparam_i_get(cfg.cur.mode());
    bool on = param_i_get(cfg.cw.tune()) && ((mode == x6100_mode_cw) || (mode == x6100_mode_cwr));
    if (on) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

}

static void fade_anim_cb(void * var, int32_t val) {
    fade_mix = val;
    lv_obj_invalidate(obj);
}

static void refresh_anim_cb(void *) {
    lv_anim_del(&fade_anim, fade_anim_cb);
    lv_anim_start(&fade_anim);
}
