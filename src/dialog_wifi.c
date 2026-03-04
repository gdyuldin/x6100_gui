/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "dialog_wifi.h"

#include "buttons.h"
#include "events.h"
#include "keyboard.h"
#include "msg.h"
#include "params/params.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "textarea_window.h"
#include "wifi.h"

#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>

#define DIALOG_WIDTH 775
#define DIALOG_HEIGHT 320
#define PARAMS_WIDTH 300

#define SIZE_OF_ARRAY(arr) (sizeof(arr) / sizeof(*arr))

enum selected_ap_type {
    SELECTED_AP_NONE,
    SELECTED_AP_UNKNOWN,
    SELECTED_AP_KNOWN,
};

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t *e);
static void cell_selected_cb(lv_event_t *e);
static void ap_table_draw_event_cb(lv_event_t *e);

// button callbacks
static void wifi_bt_toggle_cb(button_item_t *item);
static void start_scan_cb(button_item_t *item);
static void connect_cb(button_item_t *item);
static void con_change_passwd_cb(button_item_t *item);
static void con_delete_cb(button_item_t *item);

// button label getters
static const char *wifi_on_off_label_getter();
static const char *wifi_scan_label_getter();
static const char *wifi_connected_label_getter();
static const char *wifi_con_change_passwd_label_getter();
static const char *wifi_con_delete_label_getter();

static void start_refresh_ap_list();
static void stop_refresh_ap_list();
static void update_aps_table_cb(lv_timer_t *);

static int compare_aps(const void *a, const void *b);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close();

static void update_status_cb(lv_timer_t *);
static void wifi_state_changed_cb(void *s, lv_msg_t *m);

static button_item_t btn_on_off = {
    .type     = BTN_TEXT_FN,
    .label_fn = wifi_on_off_label_getter,
    .press    = wifi_bt_toggle_cb,
};
static button_item_t btn_scan = {
    .type     = BTN_TEXT_FN,
    .label_fn = wifi_scan_label_getter,
    .press    = start_scan_cb,
};
static button_item_t btn_connect = {
    .type     = BTN_TEXT_FN,
    .label_fn = wifi_connected_label_getter,
    .press    = connect_cb,
};
static button_item_t btn_change_passwd = {
    .type     = BTN_TEXT_FN,
    .label_fn = wifi_con_change_passwd_label_getter,
    .press    = con_change_passwd_cb,
};
static button_item_t btn_con_delete = {
    .type     = BTN_TEXT_FN,
    .label_fn = wifi_con_delete_label_getter,
    .press    = con_delete_cb,
};
static buttons_page_t btn_page = {
    {
     &btn_on_off,
     &btn_scan,
     &btn_connect,
     &btn_change_passwd,
     &btn_con_delete,
     }
};

static lv_timer_t *timer_refresh_ap = NULL;
static lv_timer_t *timer_status = NULL;
static void       *subscription = NULL;

static lv_obj_t *ap_table;
static lv_obj_t *label_status;
static lv_obj_t *label_ip_addr;
static lv_obj_t *label_gateway;
static lv_obj_t *label_status;

static bool                  disable_buttons = false;
static enum selected_ap_type sel_ap_type = SELECTED_AP_NONE;

static wifi_ap_info_t cur_ap_info;
static char          *cur_password = NULL;

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .btn_page = &btn_page,
    .audio_cb = NULL,
    .key_cb = key_cb,
};

dialog_t *dialog_wifi = &dialog;

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    if (params.wifi_enabled.x) {
        start_refresh_ap_list();
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

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "IP address:");
    label_ip_addr = lv_label_create(param_cont);
    lv_label_set_text(label_ip_addr, "N/A");
    lv_obj_add_style(label_ip_addr, &style_val_label, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Gateway:");
    label_gateway = lv_label_create(param_cont);
    lv_label_set_text(label_gateway, "N/A");
    lv_obj_add_style(label_gateway, &style_val_label, 0);

    // APs table
    ap_table = lv_table_create(cont);
    lv_table_set_col_cnt(ap_table, 1);
    lv_table_set_col_width(ap_table, 0, DIALOG_WIDTH - PARAMS_WIDTH - 2);
    lv_obj_set_height(ap_table, DIALOG_HEIGHT);
    lv_obj_center(ap_table);
    lv_obj_set_flex_grow(ap_table, 1);

    lv_obj_remove_style(ap_table, NULL, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(ap_table, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ap_table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ap_table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ap_table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(ap_table, 128, LV_PART_MAIN);

    lv_obj_set_style_border_width(ap_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ap_table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ap_table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(ap_table, LV_OPA_30, LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_pad_top(ap_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(ap_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(ap_table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(ap_table, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(ap_table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(ap_table, cell_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ap_table, ap_table_draw_event_cb, LV_EVENT_DRAW_PART_END, NULL);
    lv_group_add_obj(keyboard_group, ap_table);
    lv_group_set_editing(keyboard_group, true);

    subscription = lv_msg_subscribe(MSG_WIFI_STATE_CHANGED, wifi_state_changed_cb, NULL);
    lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
}

static void destruct_cb() {
    if (subscription) {
        lv_msg_unsubscribe(subscription);
        subscription = NULL;
    }
    if (timer_status) {
        lv_timer_del(timer_status);
        timer_status = NULL;
    }
    keyboard_close();
    stop_refresh_ap_list();
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
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t  col;
    uint16_t  row;
    lv_table_get_selected_cell(obj, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        sel_ap_type = SELECTED_AP_NONE;
        lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
        return;
    }
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        if (ap_info->known && (sel_ap_type != SELECTED_AP_KNOWN)) {
            sel_ap_type = SELECTED_AP_KNOWN;
            lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
        } else if (!ap_info->known && (sel_ap_type != SELECTED_AP_UNKNOWN)) {
            sel_ap_type = SELECTED_AP_UNKNOWN;
            lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
        }
    } else if (sel_ap_type != SELECTED_AP_NONE) {
        sel_ap_type = SELECTED_AP_NONE;
        lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
    }
}

/* Buttons callbacks */

static void wifi_bt_toggle_cb(button_item_t *item) {
    if (disable_buttons)
        return;
    if (params.wifi_enabled.x) {
        stop_refresh_ap_list();
        wifi_power_off();
        // clear table
        lv_table_set_cell_value(ap_table, 0, 0, "");
        lv_table_set_cell_user_data(ap_table, 0, 0, NULL);
        lv_table_set_row_cnt(ap_table, 1);
        lv_event_send(ap_table, LV_EVENT_VALUE_CHANGED, NULL);
    } else {
        wifi_power_on();
        start_refresh_ap_list();
        msg_update_text_fmt("Turning on");
    }
}

static void start_scan_cb(button_item_t *item) {
    wifi_status_t status;
    if (disable_buttons)
        return;
    status = wifi_get_status();
    if ((status == WIFI_OFF) || (status == WIFI_STARTING))
        return;
    if (wifi_scanning()) {
        msg_update_text_fmt("Already scanning");
    } else {
        msg_update_text_fmt("Start scan");
        wifi_start_scan();
    }
}

static void connect_cb(button_item_t *item) {
    uint16_t row, col;

    if (disable_buttons)
        return;

    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
        lv_table_get_selected_cell(ap_table, &row, &col);
        if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
            LV_LOG_USER("Nothing selected, can't connect");
            return;
        }
        wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
        if (ap_info->known) {
            wifi_connect(ap_info->ssid);
        } else if (ap_info->password_validator == NULL) {
            wifi_add_connection(ap_info->ssid, NULL);
        } else {
            cur_ap_info = *ap_info;
            keyboard_open();
        }
        break;
    case WIFI_CONNECTED:
        wifi_disconnect();
        break;
    case WIFI_CONNECTING:
        break;
    }
}

static void con_change_passwd_cb(button_item_t *item) {
    uint16_t    row, col;
    const char *con_id;

    lv_table_get_selected_cell(ap_table, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        LV_LOG_USER("Nothing selected");
        return;
    }
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        if (!ap_info->known) {
            msg_update_text_fmt("Can't update new connection");
            return;
        } else if (!ap_info->password_validator) {
            msg_update_text_fmt("Password is not used");
            return;
        }
        cur_ap_info = *ap_info;
        keyboard_open();
    }
}

static void con_delete_cb(button_item_t *item) {
    uint16_t    row, col;
    const char *con_id;

    lv_table_get_selected_cell(ap_table, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        LV_LOG_USER("Nothing selected");
        return;
    }
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        wifi_delete_connection(ap_info->ssid);
        msg_update_text_fmt("Connection deleted");
    }
}

/* Buttons labels getters */

static const char *wifi_on_off_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_OFF:
        return "Wifi/BT:\nOff";
    case WIFI_STARTING:
        return "Wifi/BT:\nStarting...";
    default:
        return "Wifi/BT:\nOn";
    }
}

static const char *wifi_scan_label_getter() {
    wifi_status_t status;
    status = wifi_get_status();
    if ((status == WIFI_OFF) || (status == WIFI_STARTING))
        return "";
    return wifi_scanning() ? "Scanning..." : "Scan";
}

static const char *wifi_connected_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
        if (sel_ap_type != SELECTED_AP_NONE) {
            return "Connect";
        } else {
            return "";
        }
    case WIFI_CONNECTED:
        return "Disconnect";
    case WIFI_CONNECTING:
        return "Connecting...";
    default:
        return "";
    }
}

static const char *wifi_con_change_passwd_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
    case WIFI_CONNECTED:
        if (sel_ap_type == SELECTED_AP_KNOWN) {
            return "Change\npassword";
        } else {
            return "";
        }
    default:
        return "";
    }
}

static const char *wifi_con_delete_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
    case WIFI_CONNECTED:
        if (sel_ap_type == SELECTED_AP_KNOWN) {
            return "Delete";
        } else {
            return "";
        }
    default:
        return "";
    }
}

static void start_refresh_ap_list() {
    timer_refresh_ap = lv_timer_create(update_aps_table_cb, 1000, NULL);
}

static void stop_refresh_ap_list() {
    if (timer_refresh_ap) {
        lv_timer_del(timer_refresh_ap);
        timer_refresh_ap = NULL;
    }
}

static void update_aps_table_cb(lv_timer_t *t) {
    wifi_ap_arr_t aps_info;
    uint16_t      row;
    bool          first_known = true;

    if (!params.wifi_enabled.x) {
        stop_refresh_ap_list();
    }

    aps_info = wifi_get_available_access_points();
    if (aps_info.count) {
        qsort(aps_info.ap_arr, aps_info.count, sizeof(wifi_ap_info_t), compare_aps);
    }
    row = 0;

    for (uint16_t i = 0; i < aps_info.count; i++) {
        lv_table_set_cell_value_fmt(ap_table, row++, 0, "%s %s", aps_info.ap_arr[i].ssid,
                                    aps_info.ap_arr[i].is_connected ? " (*)" : "");
        wifi_ap_info_t *copy = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t));
        *copy = aps_info.ap_arr[i];
        lv_table_set_cell_user_data(ap_table, row - 1, 0, (void *)copy);
    }

    if (row > 0) {
        lv_table_set_row_cnt(ap_table, row);
    } else {
        lv_table_set_cell_value(ap_table, 0, 0, "");
        lv_table_set_cell_user_data(ap_table, 0, 0, NULL);
        lv_table_set_row_cnt(ap_table, 1);
    }
    lv_event_send(ap_table, LV_EVENT_VALUE_CHANGED, NULL);
    wifi_aps_info_delete(aps_info);
}

static int compare_aps(const void *a, const void *b) {
    int score_a = ((wifi_ap_info_t *)a)->is_connected * 2 + ((wifi_ap_info_t *)a)->known;
    int score_b = ((wifi_ap_info_t *)b)->is_connected * 2 + ((wifi_ap_info_t *)b)->known;
    if (score_a == score_b)
        return 0;
    else if (score_a < score_b)
        return 1;
    else
        return -1;
}

static void keyboard_open() {
    lv_group_remove_obj(ap_table);
    textarea_window_open(keyboard_ok_cb, keyboard_cancel_cb);

    if (cur_password) {
        textarea_window_set(cur_password);
    } else {
        lv_obj_t *text = textarea_window_text();
        lv_textarea_set_placeholder_text(text, " Password");
    }
    disable_buttons = true;
}

static void keyboard_close() {
    textarea_window_close();
    lv_group_add_obj(keyboard_group, ap_table);
    lv_group_set_editing(keyboard_group, true);
    disable_buttons = false;
}

static bool keyboard_cancel_cb() {
    msg_update_text_fmt("Password is required");
    keyboard_close();
    return true;
}

static bool keyboard_ok_cb() {
    cur_password = (char *)textarea_window_get();

    if (cur_ap_info.password_validator) {
        if (!cur_ap_info.password_validator(cur_password)) {
            msg_update_text_fmt("Incorrect password");
            return false;
        }
    }
    if (cur_ap_info.known) {
        wifi_update_connection(cur_ap_info.ssid, cur_password);
    } else {
        wifi_add_connection(cur_ap_info.ssid, cur_password);
    }
    cur_password = NULL;
    keyboard_close();
    return true;
}

static void update_status_cb(lv_timer_t *t) {
    char  ip_address[16];
    char  gateway[16];
    char *ip_addr_p = ip_address;
    char *gateway_p = gateway;
    bool  res;
    res = wifi_get_ipaddr(&ip_addr_p, &gateway_p);
    if (res == (wifi_get_status() == WIFI_CONNECTED)) {
        if (res) {
            lv_label_set_text(label_ip_addr, ip_address);
            lv_label_set_text(label_gateway, gateway);
        } else {
            lv_label_set_text(label_ip_addr, "N/A");
            lv_label_set_text(label_gateway, "N/A");
        }
        if (timer_status) {
            lv_timer_del(timer_status);
            timer_status = NULL;
        }
    }
}

static void wifi_state_changed_cb(void *s, lv_msg_t *m) {
    buttons_page_t *page = buttons_get_cur_page();
    if (page) {
        for (size_t i = 0; i < BUTTONS; i++) {
            buttons_refresh(page->items[i]);
        }
    }

    const char *status_text;
    switch (wifi_get_status()) {
    case WIFI_CONNECTED:
        status_text = "Connected";
        break;
    case WIFI_CONNECTING:
        status_text = "Connecting...";
        break;
    case WIFI_STARTING:
        status_text = "Starting...";
    default:
        status_text = "Disconnected";
    }
    lv_label_set_text(label_status, status_text);
    if (!timer_status) {
        timer_status = lv_timer_create(update_status_cb, 100, NULL);
    }
}

static void ap_table_draw_event_cb(lv_event_t *e) {
    lv_obj_t               *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part == LV_PART_ITEMS) {
        uint32_t        row = dsc->id / lv_table_get_col_cnt(obj);
        uint32_t        col = dsc->id - row * lv_table_get_col_cnt(obj);
        wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(obj, row, col);
        if (ap_info) {
            lv_coord_t x2 = dsc->draw_area->x2;
            lv_coord_t y2 = dsc->draw_area->y2;

            lv_draw_rect_dsc_t draw_rect_dsc;
            lv_draw_rect_dsc_init(&draw_rect_dsc);
            draw_rect_dsc.bg_color = lv_color_white();
            draw_rect_dsc.bg_opa = LV_OPA_COVER;
            draw_rect_dsc.border_width = 1;
            draw_rect_dsc.border_color = lv_color_white();

            const uint8_t bars = 5;
            const uint8_t bar_w = 10;
            const uint8_t bar_spacing = 5;

            uint8_t active_cnt = ap_info->strength * bars / 100;

            lv_coord_t x1 = x2 - bars * (bar_w + bar_spacing);
            lv_coord_t max_h = dsc->draw_area->y2 - dsc->draw_area->y1 - 10;

            for (size_t i = 0; i < bars; i++) {
                lv_area_t coords;
                coords.x1 = x1;
                coords.x2 = coords.x1 + bar_w;
                coords.y2 = y2 - 5;
                coords.y1 = coords.y2 - (i + 1) * 5;
                if (i > active_cnt) {
                    draw_rect_dsc.bg_opa = LV_OPA_0;
                }
                lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &coords);
                x1 += bar_w + bar_spacing;
            }
        }
    }
}
