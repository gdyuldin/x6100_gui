/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog_settings.h"

#include "settings_context.h"

extern "C" {
#include "../events.h"
#include "../keyboard.h"
#include "../radio.h"
}

static void load_general_page(button_data_t *btn_data);
static void load_ui_page(button_data_t *btn_data);
static void load_voice_page(button_data_t *btn_data);
static void load_info_page(button_data_t *btn_data);

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t *e);

static SettingsPage page;

static button_data_t btn_general = {
    .type  = BTN_TEXT,
    .label = "General",
    .press = load_general_page,
};

static button_data_t btn_ui = {
    .type  = BTN_TEXT,
    .label = "Interface",
    .press = load_ui_page,
};

static button_data_t btn_voice = {
    .type  = BTN_TEXT,
    .label = "Voice",
    .press = load_voice_page,
};

static button_data_t btn_info = {
    .type  = BTN_TEXT,
    .label = "Info",
    .press = load_info_page,
};

buttons_page_t btn_page = {
    {
     &btn_general,
     &btn_ui,
     &btn_voice,
     &btn_info,
     }
};

static dialog_t dialog = {
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .btn_page     = &btn_page,
    .key_cb       = key_cb,
    .run          = false,
};

dialog_t *dialog_settings = &dialog;

static void load_general_page(button_data_t *btn_data) {
    make_general_page(page);
    for (auto &&btn : btn_page.items) {
        buttons_mark(btn, false);
    }
    buttons_mark(btn_data, true);
}

static void load_ui_page(button_data_t *btn_data) {
    make_ui_page(page);
    for (auto &&btn : btn_page.items) {
        buttons_mark(btn, false);
    }
    buttons_mark(btn_data, true);
}

static void load_voice_page(button_data_t *btn_data) {
    make_voice_page(page);
    for (auto &&btn : btn_page.items) {
        buttons_mark(btn, false);
    }
    buttons_mark(btn_data, true);
}

static void load_info_page(button_data_t *btn_data) {
    make_info_page(page);
    for (auto &&btn : btn_page.items) {
        buttons_mark(btn, false);
    }
    buttons_mark(btn_data, true);
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj  = dialog_init(parent);
    page.dialog = &dialog;
    make_general_page(page);
    for (auto &&btn : btn_page.items) {
        buttons_mark(btn, false);
    }
    buttons_mark(&btn_general, true);
}

static void destruct_cb() {
    page.destroy();
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case HKEY_FINP:
            lv_group_set_editing(keyboard_group, !lv_group_get_editing((const lv_group_t *)keyboard_group));
            break;

        case LV_KEY_ESC:
            dialog_destruct();
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
