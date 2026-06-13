/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include <cstring>

#include "panel.h"
#include "knobs.h"
#include "util.h"

extern "C" {
    #include "rtty.h"
    #include "styles.h"
    #include "scheduler.h"
    #include "radio.h"
    #include "params/params.h"
}

static lv_obj_t    *obj;
static lv_obj_t    *info;
static lv_anim_t    dim_anim;
static char         buf[1024] = "";
static char        *buf_write = buf;
static x6100_mode_t prev_mode;

static void update_visibility_cb(Subject *subj, void *user_data);
static void on_freq_change(Subject *subj, void *user_data);

static void set_opa(void * panel_obj, int32_t opa);


static void check_lines() {
    char        *second_line = NULL;
    char        *ptr = buf;
    uint16_t    count = 0;

    // Count lines, store start of 2nd in `second_line`
    while (*ptr) {
        if (*ptr == '\n') {
            count++;

            if (count == 1) {
                second_line = ptr + 1;
            }
        }
        ptr++;
    }

    // Too long, cut first line
    if (count > 4) {
        memmove(buf, second_line, strlen(second_line) + 1);
        buf_write = buf + strlen(buf);
    }
}

static void panel_update_text_cb(const char *text) {
    lv_point_t text_size;
    char *old_write;

    if (!buf_write) {
        return;
    }
    old_write = buf_write;
    // TODO: check len of text is not exceed width
    buf_write = stpcpy(buf_write, text);
    // new line text only
    if (strcmp(buf_write - 2, "\n\n") == 0) {
        *buf_write-- = '\0';
    } else {
        lv_txt_get_size(&text_size, buf, &sony_38, 0, 0, LV_COORD_MAX, 0);
        if (text_size.x > (lv_obj_get_width(obj) - 20)) {
            *old_write = '\n';
            buf_write = stpcpy(old_write + 1, text);
        }
    }
    check_lines();
    lv_label_set_text_static(obj, buf);
}

static void panel_update_info_cb(const char *text) {
    lv_label_set_text(info, text);
}

lv_obj_t * panel_init(lv_obj_t *parent) {
    obj = lv_label_create(parent);

    lv_label_set_text_static(obj, buf);

    lv_obj_add_style(obj, &panel_style, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_anim_init(&dim_anim);
    lv_anim_set_exec_cb(&dim_anim, set_opa);
    lv_anim_set_var(&dim_anim, obj);
    lv_anim_set_time(&dim_anim, 200);

    prev_mode = (x6100_mode_t)subject_get_int(cfg_cur.mode);

    subject_add_delayed_observer(cfg_cur.mode, update_visibility_cb, NULL);
    subject_add_delayed_observer_and_call(cfg.cw_decoder.val, update_visibility_cb, NULL);
    subject_add_delayed_observer(cfg_cur.fg_freq, on_freq_change, NULL);

    info = lv_label_create(obj);
    lv_obj_add_style(info, &panel_info_style, 0);
    lv_label_set_text(info, "");

    return obj;
}

void panel_add_text(const char * text) {
    scheduler_put((void(*)(void*))panel_update_text_cb, (void*)text, strlen(text) + 1);
}

void panel_set_info(const char *text) {
    scheduler_put((void(*)(void*))panel_update_info_cb, (void*)text, strlen(text) + 1);
}

void panel_hide() {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    knobs_display(true);
}

void panel_clear() {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == false) {
        buf[0] = '\0';
        buf_write = buf;
        lv_label_set_text_static(obj, buf);
    }
}

void panel_update_visibility(bool clear) {
    x6100_mode_t    mode = (x6100_mode_t)subject_get_int(cfg_cur.mode);
    bool            on = false;

    switch (mode) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            on = subject_get_int(cfg.cw_decoder.val);
            break;

        case x6100_mode_usb:
        case x6100_mode_lsb:
        case x6100_mode_usb_dig:
        case x6100_mode_lsb_dig:
            on = rtty_get_state() != RTTY_OFF;
            break;
    }

    if (on) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
            knobs_display(false);
        }
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        knobs_display(true);
    }
    if (clear) {
        panel_clear();
    }
}

static void update_visibility_cb(Subject *subj, void *user_data) {
    x6100_mode_t cur_mode = (x6100_mode_t)subject_get_int(cfg_cur.mode);
    x6100_mode_t tmp_mode = prev_mode;
    bool clear = true;

    // Prevent clear on preserving CW / CWR mode
    if (cur_mode == x6100_mode_cwr) {
        cur_mode = x6100_mode_cw;
    }
    if (tmp_mode == x6100_mode_cwr) {
        tmp_mode = x6100_mode_cw;
    }
    if (tmp_mode == cur_mode) {
        clear = false;
    }
    prev_mode = cur_mode;
    panel_update_visibility(clear);
}

static void set_opa(void * panel_obj, int32_t opa) {
    lv_obj_set_style_opa((lv_obj_t *)panel_obj, opa, 0);
}

static void revert_opa(struct _lv_anim_t * acct) {
    lv_anim_set_delay(&dim_anim, 200);
    lv_anim_set_values(&dim_anim, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_ready_cb(&dim_anim, NULL);
    lv_anim_start(&dim_anim);
}

static void on_freq_change(Subject *subj, void *user_data) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_anim_del(obj, set_opa);

    lv_opa_t cur_opa = lv_obj_get_style_opa(obj, 0);
    lv_anim_set_ready_cb(&dim_anim, revert_opa);
    lv_anim_set_values(&dim_anim, cur_opa, LV_OPA_40);
    lv_anim_set_delay(&dim_anim, 0);
    lv_anim_start(&dim_anim);
}
