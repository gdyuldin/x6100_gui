/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "dialog_bt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"

#include "bt.h"
#include "audio.h"
#include "wifi.h"
#include "styles.h"
#include "buttons.h"
#include "events.h"
#include "keyboard.h"
#include "msg.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "textarea_window.h"

#define PARAMS_WIDTH 300

/* Experimental: switch the BT headset profile (A2DP <-> HSP/HFP) from the 4th
 * button. Disabled by default, so the button keeps its normal Pair behaviour.
 * Set to 1 to enable; see BT_PROFILE_SWITCH.md for status and known issues. */
#define BT_PROFILE_SWITCH 0

#define SIZE_OF_ARRAY(arr) (sizeof(arr) / sizeof(*arr))

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t *e);
static void cell_selected_cb(lv_event_t *e);

// button callbacks
static void bt_toggle_cb(button_data_t *btn_data);
static void bt_scan_cb(button_data_t *btn_data);
static void bt_connect_cb(button_data_t *btn_data);
static void bt_pair_cb(button_data_t *btn_data);
static void bt_remove_cb(button_data_t *btn_data);

// button label getters
static const char *bt_on_off_label_getter();
static const char *bt_scan_label_getter();
static const char *bt_connect_label_getter();
static const char *bt_pair_label_getter();
static const char *bt_remove_label_getter();

static void start_refresh();
static void stop_refresh();
static void clear_table();
static void update_bt_table_cb(lv_timer_t *);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close();

static void bt_state_changed_cb(void *s, lv_msg_t *m);
static void bt_passkey_cb(const char *bdaddr, const char *name, bt_passkey_kind_t kind, const char *pin_hint);
static void update_info();

static button_data_t btn_on_off = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_on_off_label_getter,
    .press    = bt_toggle_cb,
};
static button_data_t btn_scan = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_scan_label_getter,
    .press    = bt_scan_cb,
};
static button_data_t btn_connect = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_connect_label_getter,
    .press    = bt_connect_cb,
};
static button_data_t btn_pair = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_pair_label_getter,
    .press    = bt_pair_cb,
};
static button_data_t btn_remove = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_remove_label_getter,
    .press    = bt_remove_cb,
};
static buttons_page_t btn_page = {
    {
     &btn_on_off,
     &btn_scan,
     &btn_connect,
     &btn_pair,
     &btn_remove,
     }
};

static lv_timer_t *timer_refresh = NULL;
static void       *subscription = NULL;

static lv_obj_t *bt_table;
static lv_obj_t *label_status;
static lv_obj_t *label_info_title;
static lv_obj_t *label_info_value;

static bool disable_buttons = false;

typedef enum {
    BT_ACT_NONE,
    BT_ACT_CONNECT,
    BT_ACT_DISCONNECT,
    BT_ACT_PAIR,
    BT_ACT_REMOVE,
    BT_ACT_PROFILE,
} bt_action_t;

static bt_action_t pending_action = BT_ACT_NONE;

static char sel_bdaddr[18];
static bool cur_dev_connected = false;
static bool cur_dev_paired = false;

static char              cur_bt_bdaddr[18];
static bt_passkey_kind_t cur_passkey_kind = BT_PASSKEY_NONE;

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .btn_page = &btn_page,
    .key_cb = key_cb,
};

dialog_t *dialog_bt = &dialog;

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    bt_set_passkey_cb(bt_passkey_cb);
    LV_LOG_USER("BT UI construct: passkey cb set");

    if (param_i_get(cfg.network.wifi_enabled())) {
        start_refresh();
    }

    // Container
    lv_obj_t *cont = lv_obj_create(dialog.obj);
    lv_obj_remove_style(cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(cont, DIALOG_WIDTH, DIALOG_HEIGHT);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    // Params container
    lv_obj_t *param_cont;
    param_cont = lv_obj_create(cont);
    lv_obj_remove_style(param_cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_style_pad_all(param_cont, 10, LV_PART_MAIN);
    lv_obj_set_size(param_cont, PARAMS_WIDTH, DIALOG_HEIGHT);
    lv_obj_set_flex_flow(param_cont, LV_FLEX_FLOW_COLUMN);

    // Params
    static lv_style_t style_val_label;
    lv_style_init(&style_val_label);
    lv_style_set_pad_bottom(&style_val_label, 20);
    lv_style_set_pad_left(&style_val_label, 10);

    lv_obj_t *label;
    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Status:");
    label_status = lv_label_create(param_cont);
    lv_label_set_text(label_status, "Unknown");
    lv_obj_add_style(label_status, &style_val_label, 0);

    label_info_title = lv_label_create(param_cont);
    lv_label_set_text(label_info_title, "Info:");
    label_info_value = lv_label_create(param_cont);
    lv_label_set_text(label_info_value, "");
    lv_obj_add_style(label_info_value, &style_val_label, 0);
    lv_label_set_long_mode(label_info_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info_value, lv_pct(100));

    // Devices table
    bt_table = lv_table_create(cont);
    lv_table_set_col_cnt(bt_table, 1);
    lv_table_set_col_width(bt_table, 0, DIALOG_WIDTH - PARAMS_WIDTH - 2);
    lv_obj_set_height(bt_table, DIALOG_HEIGHT);
    lv_obj_center(bt_table);
    lv_obj_set_flex_grow(bt_table, 1);

    lv_obj_remove_style(bt_table, NULL, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(bt_table, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bt_table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bt_table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bt_table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bt_table, 128, LV_PART_MAIN);

    lv_obj_set_style_border_width(bt_table, 0, LV_PART_ITEMS);
    lv_obj_add_style(bt_table, &style.text_base_color, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bt_table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(bt_table, LV_OPA_30, LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_pad_top(bt_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(bt_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(bt_table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(bt_table, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(bt_table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(bt_table, cell_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(keyboard_group, bt_table);
    lv_group_set_editing(keyboard_group, true);

    subscription = lv_msg_subscribe(MSG_BT_STATE_CHANGED, bt_state_changed_cb, NULL);
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

static void destruct_cb() {
    if (subscription) {
        lv_msg_unsubscribe(subscription);
        subscription = NULL;
    }
    bt_set_passkey_cb(NULL);
    LV_LOG_USER("BT UI destruct: passkey cb cleared");
    keyboard_close();
    stop_refresh();
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {

    case LV_KEY_ESC:
        dialog_destruct(&dialog);
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

static void cell_selected_cb(lv_event_t *e) {
    uint16_t row, col;
    lv_table_get_selected_cell(bt_table, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        sel_bdaddr[0]        = '\0';
        cur_dev_connected    = false;
        cur_dev_paired       = false;
        lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
        return;
    }
    bt_dev_info_t *info = (bt_dev_info_t *)lv_table_get_cell_user_data(bt_table, row, col);
    if (info) {
        snprintf(sel_bdaddr, sizeof(sel_bdaddr), "%s", info->bdaddr);
        cur_dev_connected = info->connected;
        cur_dev_paired    = info->paired;
    } else {
        sel_bdaddr[0]        = '\0';
        cur_dev_connected    = false;
        cur_dev_paired       = false;
    }
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

/* Buttons callbacks */

static void bt_toggle_cb(button_data_t *btn_data) {
    if (disable_buttons)
        return;
    if (param_i_get(cfg.network.wifi_enabled())) {
        stop_refresh();
        wifi_power_off();
        clear_table();
        lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
    } else {
        wifi_power_on();
        start_refresh();
        msg_update_text_fmt("Turning on");
    }
}

static void bt_scan_cb(button_data_t *btn_data) {
    LV_LOG_USER("BT UI scan pressed disable=%d scanning=%d", disable_buttons, bt_is_scanning());
    if (disable_buttons)
        return;
    if (!param_i_get(cfg.network.wifi_enabled()))
        return;
    if (bt_is_scanning()) {
        bt_stop_discovery();
    } else {
        bt_start_discovery();
    }
}

static void bt_connect_cb(button_data_t *btn_data) {
    LV_LOG_USER("BT UI connect pressed disable=%d sel=%s connected=%d", disable_buttons, sel_bdaddr,
                cur_dev_connected);
    if (disable_buttons || pending_action != BT_ACT_NONE || bt_is_busy())
        return;
    if (sel_bdaddr[0] == '\0')
        return;

    pending_action = cur_dev_connected ? BT_ACT_DISCONNECT : BT_ACT_CONNECT;

    if (cur_dev_connected) {
        bt_disconnect(sel_bdaddr);
    } else {
        bt_connect(sel_bdaddr);
    }

    if (!bt_is_busy()) {
        pending_action = BT_ACT_NONE;
        return;
    }
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

static void bt_pair_cb(button_data_t *btn_data) {
    LV_LOG_USER("BT UI pair pressed disable=%d sel=%s paired=%d pending=%d busy=%d", disable_buttons, sel_bdaddr,
                cur_dev_paired, pending_action, bt_is_busy());
    if (disable_buttons || pending_action != BT_ACT_NONE || bt_is_busy())
        return;

#if BT_PROFILE_SWITCH
    if (audio_bt_profile_supported()) {
        LV_LOG_USER("BT UI profile switch: supported=%d active=%d switching=%d", audio_bt_profile_supported(),
                    audio_bt_profile_get(), audio_bt_profile_switching());
        pending_action = BT_ACT_PROFILE;
        audio_bt_profile_toggle();
        LV_LOG_USER("BT UI profile toggle done: active=%d switching=%d", audio_bt_profile_get(),
                    audio_bt_profile_switching());
        if (!audio_bt_profile_switching()) {
            pending_action = BT_ACT_NONE;
        }
        lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
        return;
    }
#endif

    if (sel_bdaddr[0] == '\0')
        return;
    if (cur_dev_paired)
        return;

    pending_action = BT_ACT_PAIR;
    bt_pair(sel_bdaddr);

    if (!bt_is_busy()) {
        pending_action = BT_ACT_NONE;
        return;
    }
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

static void bt_remove_cb(button_data_t *btn_data) {
    LV_LOG_USER("BT UI remove pressed disable=%d sel=%s", disable_buttons, sel_bdaddr);
    if (disable_buttons || pending_action != BT_ACT_NONE || bt_is_busy())
        return;
    if (sel_bdaddr[0] == '\0')
        return;

    pending_action = BT_ACT_REMOVE;
    bt_remove(sel_bdaddr);

    if (!bt_is_busy()) {
        pending_action = BT_ACT_NONE;
        return;
    }
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

/* Buttons labels getters */

static const char *bt_action_text(bt_action_t action) {
    switch (action) {
    case BT_ACT_CONNECT:
        return "Connecting...";
    case BT_ACT_DISCONNECT:
        return "Disconnecting...";
    case BT_ACT_PAIR:
        return "Pairing...";
    case BT_ACT_REMOVE:
        return "Removing...";
    case BT_ACT_PROFILE:
        return "Switching...";
    default:
        return "";
    }
}

static const char *bt_on_off_label_getter() {
    if (param_i_get(cfg.network.wifi_enabled())) {
        return "Wifi/BT:\nOn";
    } else {
        return "Wifi/BT:\nOff";
    }
}

static const char *bt_scan_label_getter() {
    if (!param_i_get(cfg.network.wifi_enabled()))
        return "";
    return bt_is_scanning() ? "Scanning..." : "Scan";
}

static const char *bt_connect_label_getter() {
    if (sel_bdaddr[0] == '\0')
        return "";
    if (pending_action == BT_ACT_CONNECT || pending_action == BT_ACT_DISCONNECT)
        return bt_action_text(pending_action);
    return cur_dev_connected ? "Disconnect" : "Connect";
}

static const char *bt_pair_label_getter() {
#if BT_PROFILE_SWITCH
    if (audio_bt_profile_supported()) {
        if (audio_bt_profile_switching() || pending_action == BT_ACT_PROFILE)
            return "Switching...";
        switch (audio_bt_profile_get()) {
        case AUDIO_BT_PROFILE_A2DP:
            return "Profile:\nA2DP";
        case AUDIO_BT_PROFILE_HFP:
            return "Profile:\nHSP/HFP";
        default:
            return "";
        }
    }
#endif
    if (sel_bdaddr[0] == '\0')
        return "";
    if (pending_action == BT_ACT_PAIR)
        return bt_action_text(pending_action);
    return cur_dev_paired ? "" : "Pair";
}

static const char *bt_remove_label_getter() {
    if (sel_bdaddr[0] == '\0')
        return "";
    if (pending_action == BT_ACT_REMOVE)
        return bt_action_text(pending_action);
    return "Remove";
}

/* Refresh */

static void start_refresh() {
    if (!timer_refresh) {
        timer_refresh = lv_timer_create(update_bt_table_cb, 1000, NULL);
    }
}

static void stop_refresh() {
    if (timer_refresh) {
        lv_timer_del(timer_refresh);
        timer_refresh = NULL;
    }
}

static void clear_table() {
    lv_table_set_cell_value(bt_table, 0, 0, "");
    lv_table_set_cell_user_data(bt_table, 0, 0, NULL);
    lv_table_set_row_cnt(bt_table, 1);
    lv_event_send(bt_table, LV_EVENT_VALUE_CHANGED, NULL);
}

static void update_bt_table_cb(lv_timer_t *t) {
    bt_dev_arr_t devs;
    uint16_t     row;

    if (!param_i_get(cfg.network.wifi_enabled())) {
        stop_refresh();
        return;
    }

    devs = bt_get_devices();

    row = 0;
    for (uint16_t i = 0; i < devs.count; i++) {
        lv_table_set_cell_value_fmt(bt_table, row++, 0, "%s %s\n%s", devs.dev_arr[i].name,
                                    devs.dev_arr[i].connected ? "(*)" : (devs.dev_arr[i].paired ? "(P)" : ""),
                                    devs.dev_arr[i].bdaddr);
        bt_dev_info_t *copy = (bt_dev_info_t *)lv_mem_alloc(sizeof(bt_dev_info_t));
        *copy               = devs.dev_arr[i];
        lv_table_set_cell_user_data(bt_table, row - 1, 0, (void *)copy);
    }

    if (row > 0) {
        lv_table_set_row_cnt(bt_table, row);
    } else {
        lv_table_set_cell_value(bt_table, 0, 0, "");
        lv_table_set_cell_user_data(bt_table, 0, 0, NULL);
        lv_table_set_row_cnt(bt_table, 1);
    }
    lv_event_send(bt_table, LV_EVENT_VALUE_CHANGED, NULL);
    bt_devices_info_delete(devs);
}

/* Keyboard / passkey */

static void keyboard_open() {
    lv_group_remove_obj(bt_table);
    textarea_window_open(keyboard_ok_cb, keyboard_cancel_cb);
    disable_buttons = true;
}

static void keyboard_close() {
    textarea_window_close();
    lv_group_add_obj(keyboard_group, bt_table);
    lv_group_set_editing(keyboard_group, true);
    disable_buttons = false;
}

static bool keyboard_cancel_cb() {
    bt_agent_respond(cur_bt_bdaddr, false, NULL);
    cur_passkey_kind = BT_PASSKEY_NONE;
    keyboard_close();
    return true;
}

static bool keyboard_ok_cb() {
    const char *pin = (const char *)textarea_window_get();
    bt_agent_respond(cur_bt_bdaddr, true, pin ? pin : "");
    cur_passkey_kind = BT_PASSKEY_NONE;
    keyboard_close();
    return true;
}

static void bt_passkey_cb(const char *bdaddr, const char *name, bt_passkey_kind_t kind, const char *pin_hint) {
    LV_LOG_USER("BT UI passkey kind=%d bdaddr=%s name=%s hint=%s", kind, bdaddr ? bdaddr : "", name ? name : "",
                pin_hint ? pin_hint : "");
    snprintf(cur_bt_bdaddr, sizeof(cur_bt_bdaddr), "%s", bdaddr);
    cur_passkey_kind = kind;

    keyboard_open();

    if (kind == BT_PASSKEY_CONFIRM) {
        if (pin_hint && pin_hint[0]) {
            textarea_window_set(pin_hint);
        } else {
            lv_obj_t *text = textarea_window_text();
            lv_textarea_set_placeholder_text(text, " Confirm");
        }
        msg_update_text_fmt("Confirm passkey for %s", name[0] ? name : bdaddr);
    } else {
        lv_obj_t *text = textarea_window_text();
        lv_textarea_set_placeholder_text(text, (kind == BT_PASSKEY_PIN) ? " PIN" : " Passkey");
        msg_update_text_fmt("Enter passkey for %s", name[0] ? name : bdaddr);
    }
}

/* State change */

static void update_info() {
    bt_dev_arr_t devs  = bt_get_devices();
    bool         found = false;
    bt_dev_info_t info;

    for (uint8_t i = 0; i < devs.count; i++) {
        if (devs.dev_arr[i].connected) {
            info  = devs.dev_arr[i];
            found = true;
            break;
        }
    }
    bt_devices_info_delete(devs);

    if (found) {
        char class_text[64];
        bt_device_class_text(&info, class_text, sizeof(class_text));
        if (info.has_battery) {
            lv_label_set_text_fmt(label_info_value, "%s\n%s\nBattery: %u%%", info.name[0] ? info.name : info.bdaddr,
                                  class_text, info.battery);
        } else {
            lv_label_set_text_fmt(label_info_value, "%s\n%s", info.name[0] ? info.name : info.bdaddr, class_text);
        }
        lv_obj_clear_flag(label_info_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_info_value, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label_info_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_info_value, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_selected_state() {
    if (sel_bdaddr[0] == '\0')
        return;

    bt_dev_arr_t devs = bt_get_devices();

    for (uint8_t i = 0; i < devs.count; i++) {
        if (strcmp(devs.dev_arr[i].bdaddr, sel_bdaddr) == 0) {
            cur_dev_connected = devs.dev_arr[i].connected;
            cur_dev_paired    = devs.dev_arr[i].paired;
            break;
        }
    }
    bt_devices_info_delete(devs);
}

static void bt_state_changed_cb(void *s, lv_msg_t *m) {
    bt_status_t status = bt_get_status();

    if (pending_action == BT_ACT_PROFILE) {
        LV_LOG_USER("BT UI state change: profile pending, switching=%d active=%d", audio_bt_profile_switching(),
                    audio_bt_profile_get());
        if (!audio_bt_profile_switching()) {
            pending_action = BT_ACT_NONE;
        }
    } else if (pending_action != BT_ACT_NONE && (!bt_is_busy() || status == BT_OFF)) {
        if (pending_action == BT_ACT_REMOVE) {
            sel_bdaddr[0]     = '\0';
            cur_dev_connected = false;
            cur_dev_paired    = false;
        }
        pending_action = BT_ACT_NONE;
    }

    refresh_selected_state();

    buttons_page_t *page = buttons_get_cur_page();
    if (page) {
        for (size_t i = 0; i < BUTTONS; i++) {
            buttons_refresh(page->items[i]);
        }
    }

    const char *status_text;
    if (pending_action != BT_ACT_NONE) {
        status_text = bt_action_text(pending_action);
    } else {
        switch (status) {
        case BT_CONNECTED:
            status_text = "Connected";
            break;
        case BT_CONNECTING:
            status_text = "Connecting...";
            break;
        case BT_OFF:
            status_text = "Off";
            break;
        default:
            status_text = "On";
        }
    }
    lv_label_set_text(label_status, status_text);

    update_info();
}
