/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "events.h"
#include "radio.h"
#include "keyboard.h"
#include "textarea_window.h"
#include "styles.h"

static lv_obj_t             *window = NULL;
static lv_obj_t             *label = NULL;
static lv_obj_t             *text = NULL;
static lv_obj_t             *keyboard = NULL;

static textarea_window_cb_t ok_cb = NULL;
static textarea_window_cb_t cancel_cb = NULL;

/* The key that closes the window is usually still held. Closing deletes
 * the focused object, which resets LVGL's keypad state with a press time
 * of 0 (boot), so the held key counts as a long press and repeats into
 * whatever has the focus next: one ESC here closed the whole app. Ignore
 * that key until it's released. */
static void swallow_key() {
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) lv_indev_wait_release(indev);
}

static void ok() {
    swallow_key();
    if (ok_cb) {
        if(ok_cb()) {
            ok_cb = NULL;
            textarea_window_close();
        }
    }
 }

static void cancel() {
    swallow_key();
    if (cancel_cb) {
        cancel_cb();
        cancel_cb = NULL;
    }

    textarea_window_close();
}

static void text_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case HKEY_FINP:
        case LV_KEY_ENTER:
            ok();
            break;

        case LV_KEY_ESC:
            cancel();
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}


static void keyboard_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t        *key = ((uint32_t *)lv_event_get_param(e));

    switch (code) {
        case LV_EVENT_KEY:
            switch (*key) {
                case KEY_VOL_LEFT_EDIT:
                case KEY_VOL_LEFT_SELECT:
                    radio_change_vol(-1);
                    break;

                case KEY_VOL_RIGHT_EDIT:
                case KEY_VOL_RIGHT_SELECT:
                    radio_change_vol(1);
                    break;
            }
            break;

        case LV_EVENT_READY:
            ok();
            break;

        case LV_EVENT_CANCEL:
            cancel();
            break;
    }
}

lv_obj_t * textarea_window_open(textarea_window_cb_t ok, textarea_window_cb_t cancel) {
    ok_cb = ok;
    cancel_cb = cancel;

    window = lv_obj_create(lv_scr_act());

    lv_obj_remove_style_all(window);

    lv_obj_add_style(window, &style.msg, 0);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_y(window, 80);

    lv_obj_t * obj = lv_obj_create(window);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_remove_style(obj, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_height(obj, 35);
    lv_obj_set_width(obj, 560);
    lv_obj_center(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    lv_obj_t * item_wrapper;
    item_wrapper = lv_obj_create(obj);
    lv_obj_remove_style(item_wrapper, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(item_wrapper, LV_SIZE_CONTENT, LV_PCT(100));

    label = lv_label_create(item_wrapper);
    lv_obj_set_style_text_font(label, &sony_36, 0);
    lv_label_set_text(label, "");
    lv_obj_align_to(label, item_wrapper, LV_ALIGN_LEFT_MID, 0, 0);

    text = lv_textarea_create(obj);

    lv_obj_remove_style(text, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_add_style(text, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(text, lv_color_white(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(text, LV_OPA_80, LV_PART_CURSOR);

    lv_textarea_set_one_line(text, true);
    lv_textarea_set_max_length(text, 64);

    lv_obj_clear_flag(text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(text, &sony_44, 0);
    lv_obj_set_flex_grow(text, 1);

    if (ok || cancel) {
        lv_obj_add_event_cb(text, text_cb, LV_EVENT_KEY, NULL);
    }

    if (!keyboard_ready()) {
        keyboard = lv_keyboard_create(lv_scr_act());

        lv_keyboard_set_textarea(keyboard, text);
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_KEY, NULL);

        lv_obj_set_style_bg_color(keyboard, style.colors.mark, LV_PART_MAIN);
        lv_obj_add_style(keyboard, &style.dialog.item_focus, LV_STATE_FOCUSED | LV_PART_ITEMS);

        lv_group_add_obj(keyboard_group, keyboard);
    } else {
        keyboard = NULL;
    }

    lv_group_add_obj(keyboard_group, text);

    return window;
}

lv_obj_t *textarea_window_open_w_label(textarea_window_cb_t ok_cb, textarea_window_cb_t cancel_cb, const char *text) {
    lv_obj_t * obj = textarea_window_open(ok_cb, cancel_cb);
    lv_label_set_text(label, text);
    return obj;
}

void textarea_window_close() {
    if (keyboard) {
        lv_obj_del(keyboard);
        keyboard = NULL;
    }

    if (window) {
        lv_obj_del(window);
        window = NULL;
    }
}

const char* textarea_window_get() {
    return lv_textarea_get_text(text);
}

void textarea_window_set(const char *val) {
    lv_textarea_set_text(text, val);
}

lv_obj_t * textarea_window_text() {
    return text;
}
