/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include <cstring>

#include "panel.h"
#include "util.h"
#include "cfg/cfg_api.h"
#include "scheduler.h"

extern "C" {
    #include "rtty.h"
    #include "styles.h"
    #include "radio.h"
}

static lv_obj_t    *obj;
static lv_obj_t    *info;
static lv_anim_t    dim_anim;
static char         buf[1024] = "";
static char        *buf_write = buf;
static uint8_t      max_line_count;
static x6100_mode_t prev_mode;

static void update_visibility_cb(Subject *subj, void *user_data);
static void on_freq_change(Subject *subj, void *user_data);

static void set_opa(void * panel_obj, int32_t opa);
static void update_line_count();


static void truncate() {
    char        *second_line = NULL;
    char        *ptr = buf;
    uint16_t    count = 1;

    // Count lines, store start of 2nd in `second_line`
    while (*ptr) {
        if (*ptr == '\n') {
            count++;

            if (count == 2) {
                second_line = ptr + 1;
            }
        }
        ptr++;
    }
    if (ptr == buf) {
        return;
    }
    // Skip last empty line, if exists
    if (*(ptr - 1) == '\n') {
        count--;
    }
    // Too long, cut first line
    if (count > max_line_count) {
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
    const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    if (buf_write - buf >= 2) {
        // new line text only
        if (strcmp(buf_write - 2, "\n\n") == 0) {
            *buf_write-- = '\0';
        } else {
            lv_txt_get_size(&text_size, buf, font, 0, 0, LV_COORD_MAX, 0);
            if (text_size.x > (lv_obj_get_width(obj) - 20)) {
                *old_write = '\n';
                buf_write = stpcpy(old_write + 1, text);
            }
        }
    }
    truncate();
    lv_label_set_text_static(obj, buf);
}

static void panel_update_info_cb(const char *text) {
    lv_label_set_text(info, text);
}

lv_obj_t * panel_init(lv_obj_t *parent) {
    obj = lv_label_create(parent);

    lv_label_set_text_static(obj, buf);

    lv_obj_add_style(obj, &style.panels.base, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    panel_hide();
    lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    update_line_count();

    lv_anim_init(&dim_anim);
    lv_anim_set_exec_cb(&dim_anim, set_opa);
    lv_anim_set_var(&dim_anim, obj);
    lv_anim_set_time(&dim_anim, 200);

    prev_mode = (x6100_mode_t)cfg.cur.mode()->get();

    cfg.cur.mode()->subscribe_delayed(update_visibility_cb);
    cfg.cw.decoder()->subscribe_delayed_and_notify(update_visibility_cb);
    cfg.cur.fg_freq()->subscribe_delayed(on_freq_change);

    info = lv_label_create(obj);
    lv_obj_add_style(info, &style.panels.info, 0);
    lv_label_set_text(info, "");

    return obj;
}

void panel_set_height(lv_coord_t h) {
    styles_panel_set_height(h);
    update_line_count();
    truncate();
}

void panel_add_text(const char * text) {
    scheduler_put((void(*)(void*))panel_update_text_cb, (void*)text, strlen(text) + 1);
}

void panel_set_info(const char *text) {
    scheduler_put((void(*)(void*))panel_update_info_cb, (void*)text, strlen(text) + 1);
}

void panel_hide() {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    scheduler_msg_send(MSG_PANEL_HIDE, NULL);
}

void panel_clear() {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == false) {
        buf[0] = '\0';
        buf_write = buf;
        lv_label_set_text_static(obj, buf);
    }
}

void panel_update_visibility(bool clear) {
    x6100_mode_t    mode = (x6100_mode_t)cfg.cur.mode()->get();
    bool            on = false;

    switch (mode) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            on = cfg.cw.decoder()->get();
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
            scheduler_msg_send(MSG_PANEL_SHOW, NULL);
        }
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        scheduler_msg_send(MSG_PANEL_HIDE, NULL);
    }
    if (clear) {
        panel_clear();
    }
}

static void update_line_count() {
    lv_obj_update_layout(obj);
    lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
    const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    // TODO: check for padding
    max_line_count = (lv_obj_get_content_height(obj) + line_space) / (line_space + font->line_height);
}

static void update_visibility_cb(Subject *subj, void *user_data) {
    x6100_mode_t cur_mode = (x6100_mode_t)cfg.cur.mode()->get();
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
