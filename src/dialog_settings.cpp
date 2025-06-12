/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog_settings.h"

#include "voice.h"

extern "C" {

    #include "cfg/transverter.h"
    #include "dialog.h"
    #include "styles.h"
    #include "params/params.h"
    #include "backlight.h"
    #include "radio.h"
    #include "events.h"
    #include "keyboard.h"
    #include "clock.h"
    #include "audio.h"

    #include "lvgl/lvgl.h"
    #include <sys/time.h>
    #include <time.h>
    #include <fcntl.h>
    #include <stdlib.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <linux/rtc.h>
    #include <errno.h>

}

#define SMALL_PAD   5

#define SMALL_1     57
#define SMALL_2     (SMALL_1 * 2 + SMALL_PAD * 1)
#define SMALL_3     (SMALL_1 * 3 + SMALL_PAD * 2)
#define SMALL_4     (SMALL_1 * 4 + SMALL_PAD * 3)
#define SMALL_5     (SMALL_1 * 5 + SMALL_PAD * 4)
#define SMALL_6     (SMALL_1 * 6 + SMALL_PAD * 5)

#define SMALL_WIDTH 57

#define NUM_PAGES 2
enum {
    PAGE_GENERAL,
    PAGE_UI,
} Page;

static void make_general_page();
static void make_ui_page();

static void load_general_page(button_item_t *item);
static void load_ui_page(button_item_t *item);

static lv_obj_t     *grid;
static lv_coord_t   col_dsc[] = { 740 - (SMALL_1 + SMALL_PAD) * 6, SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, LV_GRID_TEMPLATE_LAST };
static lv_coord_t   row_dsc[64] = { 1 };

static bool         datetime_changed;
static time_t       now;
struct tm           ts;

static lv_obj_t     *day;
static lv_obj_t     *month;
static lv_obj_t     *year;
static lv_obj_t     *hour;
static lv_obj_t     *min;
static lv_obj_t     *sec;

static button_item_t btn_general = {
    .type  = BTN_TEXT,
    .label = "General",
    .press = load_general_page,
};

static button_item_t btn_ui = {
    .type  = BTN_TEXT,
    .label = "Interface",
    .press = load_ui_page,
};

buttons_page_t btn_page = {
    {
     &btn_general,
     &btn_ui,
     }
};

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);

static dialog_t     dialog = {
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = NULL,
    .btn_page = &btn_page,
    .key_cb = key_cb,
    .run = false,
};

dialog_t            *dialog_settings = &dialog;

static ObserverDelayed *output_gain_observer;

static void load_general_page(button_item_t *item) {
    make_general_page();
    buttons_mark(&btn_ui, false);
    buttons_mark(item, true);
}

static void load_ui_page(button_item_t *item) {
    make_ui_page();
    buttons_mark(&btn_general, false);
    buttons_mark(item, true);
}

/* Shared update */

static void bool_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_bool_t   *var = (params_bool_t*)lv_event_get_user_data(e);

    params_bool_set(var, lv_obj_has_state(obj, LV_STATE_CHECKED));
}

static void bool_update_subj_cb(lv_event_t *e) {
    lv_obj_t *obj  = lv_event_get_target(e);
    Subject  *subj = (Subject *)lv_event_get_user_data(e);

    subject_set_int(subj, lv_obj_has_state(obj, LV_STATE_CHECKED));
}

static void uint8_spinbox_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_uint8_t  *var = (params_uint8_t*)lv_event_get_user_data(e);

    params_uint8_set(var, lv_spinbox_get_value(obj));
}

static void uint8_dropdown_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_uint8_t  *var = (params_uint8_t*)lv_event_get_user_data(e);

    params_uint8_set(var, lv_dropdown_get_selected(obj));
}

static void theme_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_uint8_t  *var = (params_uint8_t*)lv_event_get_user_data(e);

    params_uint8_set(var, lv_dropdown_get_selected(obj));
    styles_set_theme((themes_t)var->x);
}

/* Shared create */
static lv_obj_t * create_switch(lv_obj_t *parent){
    lv_obj_t *obj = lv_switch_create(parent);
    dialog_item(&dialog, obj);
    lv_obj_center(obj);
    return obj;
}

static lv_obj_t * switch_bool(lv_obj_t *parent, params_bool_t *var) {
    lv_obj_t *obj = create_switch(parent);
    lv_obj_add_event_cb(obj, bool_update_cb, LV_EVENT_VALUE_CHANGED, var);
    if (var->x) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    return obj;
}

static lv_obj_t * switch_bool(lv_obj_t *parent, Subject *subj) {
    lv_obj_t *obj = create_switch(parent);
    lv_obj_add_event_cb(obj, bool_update_subj_cb, LV_EVENT_VALUE_CHANGED, subj);

    if (subject_get_int(subj)) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    return obj;
}

static lv_obj_t * spinbox_uint8(lv_obj_t *parent, params_uint8_t *var) {
    lv_obj_t *obj = lv_spinbox_create(parent);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, var->x);
    lv_spinbox_set_range(obj, var->min, var->max);
    lv_obj_add_event_cb(obj, uint8_spinbox_update_cb, LV_EVENT_VALUE_CHANGED, var);

    return obj;
}

static lv_obj_t * dropdown_uint8(lv_obj_t *parent, params_uint8_t *var, const char *options, lv_event_cb_t cb=uint8_dropdown_update_cb) {
    lv_obj_t *obj = lv_dropdown_create(parent);

    dialog_item(&dialog, obj);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, var);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_add_style(list, &dialog_dropdown_list_style, 0);

    lv_dropdown_set_options(obj, options);
    lv_dropdown_set_symbol(obj, NULL);

    lv_dropdown_set_selected(obj, var->x);

    return obj;
}

template <typename T>
lv_obj_t *slider_with_text(lv_obj_t *cell, T val, T min, T max, T step, size_t width, const char *fmt,
                           lv_event_cb_t event_cb, void *cb_user_data = nullptr) {
    lv_obj_t *obj;
    obj = lv_slider_create(cell);

    dialog_item(&dialog, obj);

    lv_slider_set_mode(obj, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_range(obj, min / step, max / step);
    lv_slider_set_value(obj, val / step, LV_ANIM_OFF);
    lv_obj_set_width(obj, width);

    /*Create a label below the slider*/
    lv_obj_t *slider_label = lv_label_create(cell);
    lv_obj_set_user_data(slider_label, (void *)fmt);
    lv_label_set_text_fmt(slider_label, fmt, val);
    lv_obj_align(slider_label, LV_ALIGN_RIGHT_MID, 12, 0);
    lv_obj_set_style_text_color(slider_label, lv_color_white(), 0);

    lv_obj_set_user_data(obj, slider_label);

    lv_obj_add_event_cb(obj, event_cb, LV_EVENT_VALUE_CHANGED, cb_user_data);
    return obj;
}

/**
 * Update bg opacity for some UI elements to preview changes
 */
static void change_bg_opa_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (
        (lv_obj_check_type(obj, &lv_slider_class) & lv_obj_has_state(obj, LV_STATE_EDITED)) |
        (lv_obj_check_type(obj, &lv_switch_class) & (code == LV_EVENT_FOCUSED))
    ) {
        lv_obj_set_style_bg_img_opa(dialog.obj, LV_OPA_60, 0);
    } else {
        lv_obj_set_style_bg_img_opa(dialog.obj, LV_OPA_COVER, 0);
    }
}

/* Datetime */

static void datetime_update_cb(lv_event_t * e) {
    ts.tm_mday = lv_spinbox_get_value(day);
    ts.tm_mon = lv_spinbox_get_value(month) - 1;
    ts.tm_year = lv_spinbox_get_value(year) - 1900;
    ts.tm_hour = lv_spinbox_get_value(hour);
    ts.tm_min = lv_spinbox_get_value(min);
    ts.tm_sec = lv_spinbox_get_value(sec);

    datetime_changed = true;

    /* Set system */

    struct timespec tp;

    tp.tv_sec = mktime(&ts);
    tp.tv_nsec = 0;

    int res = clock_settime(CLOCK_REALTIME, &tp);
    if (res != 0)
    {
        LV_LOG_ERROR("Can't set system time: %s\n", strerror(errno));
        return;
    }

}

static void datetime_set_rtc_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    if (!lv_obj_has_state(obj, LV_STATE_EDITED) && datetime_changed)
    {
        /* Set RTC */

        int rtc = open("/dev/rtc1", O_WRONLY);

        if (rtc > 0) {
            int res = ioctl(rtc, RTC_SET_TIME, &ts);
            if (res != 0)
            {
                LV_LOG_ERROR("Can't set RTC time: %s\n", strerror(errno));
                return;
            }
            close(rtc);
        } else {
            LV_LOG_ERROR("Can't open /dev/rtc1: %s\n", strerror(errno));
        }
        datetime_changed = false;
    }

}

static uint8_t make_date(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Day, Month, Year");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Day */

    obj = lv_spinbox_create(grid);
    day = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mday);
    lv_spinbox_set_range(obj, 1, 31);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Month */

    obj = lv_spinbox_create(grid);
    month = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mon + 1);
    lv_spinbox_set_range(obj, 1, 12);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Year */

    obj = lv_spinbox_create(grid);
    year = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_year + 1900);
    lv_spinbox_set_range(obj, 2020, 2038);
    lv_spinbox_set_digit_format(obj, 4, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    return row + 1;
}

static uint8_t make_time(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Hour, Min, Sec");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Hour */

    obj = lv_spinbox_create(grid);
    hour = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_hour);
    lv_spinbox_set_range(obj, 0, 23);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Min */

    obj = lv_spinbox_create(grid);
    min = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_min);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Sec */

    obj = lv_spinbox_create(grid);
    sec = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_sec);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    return row + 1;
}

/* Backlight */

static void backlight_timeout_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    params_lock();
    params.brightness_timeout = lv_spinbox_get_value(obj);
    params_unlock(&params.dirty.brightness_timeout);

    backlight_tick();
}

static void backlight_brightness_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    params_lock();
    params.brightness_normal = lv_slider_get_value(obj);
    params_unlock(&params.dirty.brightness_normal);

    params_lock();
    params.brightness_idle = lv_slider_get_left_value(obj);
    params_unlock(&params.dirty.brightness_idle);

    backlight_set_brightness(params.brightness_normal);
}

static void backlight_buttons_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    backlight_set_buttons((buttons_light_t)lv_dropdown_get_selected(obj));
}

static uint8_t make_backlight(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Timeout, Brightness");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Timeout */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, params.brightness_timeout);
    lv_spinbox_set_range(obj, 5, 120);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, backlight_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Brightness */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_4, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 4, LV_GRID_ALIGN_CENTER, row, 1);   col += 4;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = lv_slider_create(obj);

    dialog_item(&dialog, obj);

    lv_slider_set_mode(obj, LV_SLIDER_MODE_RANGE);
    lv_slider_set_value(obj, params.brightness_normal, LV_ANIM_OFF);
    lv_slider_set_left_value(obj, params.brightness_idle, LV_ANIM_OFF);
    lv_slider_set_range(obj, -1, 9);
    lv_obj_set_width(obj, SMALL_4 - 30);
    lv_obj_center(obj);

    lv_obj_add_event_cb(obj, backlight_brightness_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row++;
    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Buttons brightness");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);

    dialog_item(&dialog, obj);

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_add_style(list, &dialog_dropdown_list_style, 0);

    lv_dropdown_set_options(obj, " Always Off \n Always On \n Temporarily On ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, params.brightness_buttons);
    lv_obj_add_event_cb(obj, backlight_buttons_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return row + 1;
}

/* Line-in, Line-out */

static void line_in_out_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    void (*fn)(uint8_t) = (void (*)(uint8_t))lv_event_get_user_data(e);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    int32_t val = lv_slider_get_value(obj);
    fn(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static uint8_t make_line_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Line-in, Line-out");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, (int)params.line_in, 0, 36, 1, SMALL_3 - 30 - 60, "%d", line_in_out_update_cb, (void*)radio_set_line_in);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, (int)params.line_out, 0, 36, 1, SMALL_3 - 30 - 60, "%d", line_in_out_update_cb, (void*)radio_set_line_out);

    return row + 1;
}

/* Mag Freq, Info, ALC */

static uint8_t make_mag(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Mag Freq, Info, ALC");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Freq */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_freq);

    lv_obj_set_width(obj, SMALL_2 - 30);

    /* Info */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_info);

    lv_obj_set_width(obj, SMALL_2 - 30);

    /* ALC */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_alc);

    lv_obj_set_width(obj, SMALL_2 - 30);

    return row + 1;
}

/* Clock */

static void clock_view_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    clock_set_view((clock_view_t)lv_dropdown_get_selected(obj));
}

static void clock_time_timeout_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    clock_set_time_timeout(lv_spinbox_get_value(obj));
}

static void clock_power_timeout_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    clock_set_power_timeout(lv_spinbox_get_value(obj));
}

static void clock_tx_timeout_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    clock_set_tx_timeout(lv_spinbox_get_value(obj));
}

static uint8_t make_clock(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Clock view");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);

    dialog_item(&dialog, obj);

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_add_style(list, &dialog_dropdown_list_style, 0);

    lv_dropdown_set_options(obj, " Always Time \n Time and Power \n Always Power");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, params.clock_view);
    lv_obj_add_event_cb(obj, clock_view_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* * */

    row++;
    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Timeout Clock, Power, TX");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, params.clock_time_timeout);
    lv_spinbox_set_range(obj, 1, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, clock_time_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, params.clock_power_timeout);
    lv_spinbox_set_range(obj, 1, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, clock_power_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, params.clock_tx_timeout);
    lv_spinbox_set_range(obj, 0, 10);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, clock_tx_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return row + 1;
}

/* Long press actions */

typedef struct {
    const char                *label;
    press_action_t      action;
} action_items_t;

static action_items_t long_action_items[] = {
    { .label = " None ", .action = ACTION_NONE },
    { .label = " Screenshot ", .action = ACTION_SCREENSHOT },
    { .label = " Recorder on/off ", .action = ACTION_RECORDER },
    { .label = " Mute ", .action = ACTION_MUTE },
    { .label = " Voice mode ", .action = ACTION_VOICE_MODE },
    { .label = " Battery info ", .action = ACTION_BAT_INFO },
    { .label = " APP RTTY ", .action = ACTION_APP_RTTY },
    { .label = " APP FT8 ", .action = ACTION_APP_FT8 },
    { .label = " APP SWR Scan ", .action = ACTION_APP_SWRSCAN },
    { .label = " APP GPS ", .action = ACTION_APP_GPS },
    { .label = " APP Settings", .action = ACTION_APP_SETTINGS },
    { .label = " APP Recorder", .action = ACTION_APP_RECORDER },
    { .label = " QTH Grid", .action = ACTION_APP_QTH },
    { .label = NULL, .action = ACTION_NONE }
};

static void long_action_update_cb(lv_event_t * e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    uint32_t    *i = (uint32_t*)lv_event_get_user_data(e);
    uint8_t     val = long_action_items[lv_dropdown_get_selected(obj)].action;

    params_lock();

    switch (*i) {
        case 0:
            params.long_gen = val;
            params_unlock(&params.dirty.long_gen);
            break;

        case 1:
            params.long_app = val;
            params_unlock(&params.dirty.long_app);
            break;

        case 2:
            params.long_key = val;
            params_unlock(&params.dirty.long_key);
            break;

        case 3:
            params.long_msg = val;
            params_unlock(&params.dirty.long_msg);
            break;

        case 4:
            params.long_dfn = val;
            params_unlock(&params.dirty.long_dfn);
            break;

        case 5:
            params.long_dfl = val;
            params_unlock(&params.dirty.long_dfl);
            break;
    }
}

static uint8_t make_long_action(uint8_t row) {
    const char        *labels[] = {
        "GEN long press",
        "APP long press",
        "KEY long press",
        "MSG long press",
        "DFN long press",
        "DFL long press",
    };
    lv_obj_t    *obj;

    for (uint8_t i = 0; i < 6; i++) {

        obj = lv_label_create(grid);

        lv_label_set_text(obj, labels[i]);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

        obj = lv_dropdown_create(grid);

        dialog_item(&dialog, obj);

        lv_obj_set_size(obj, SMALL_6, 56);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_center(obj);

        lv_obj_t *list = lv_dropdown_get_list(obj);
        lv_obj_add_style(list, &dialog_dropdown_list_style, 0);

        lv_dropdown_set_symbol(obj, NULL);

        uint8_t x;

        switch (i) {
            case 0: x = params.long_gen;    break;
            case 1: x = params.long_app;    break;
            case 2: x = params.long_key;    break;
            case 3: x = params.long_msg;    break;
            case 4: x = params.long_dfn;    break;
            case 5: x = params.long_dfl;    break;

            default:
                x = ACTION_NONE;
                break;
        }

        lv_dropdown_clear_options(obj);

        uint8_t n = 0;

        while (long_action_items[n].label) {
            lv_dropdown_add_option(obj, long_action_items[n].label, LV_DROPDOWN_POS_LAST);

            if (long_action_items[n].action == x) {
                lv_dropdown_set_selected(obj, n);
            }

            n++;
        }

        uint32_t *param = (uint32_t*)malloc(sizeof(uint32_t));
        *param = i;

        lv_obj_add_event_cb(obj, long_action_update_cb, LV_EVENT_VALUE_CHANGED, param);

        row++;
    }

    return row;
}

/* HMic F1, F2 actions */

static action_items_t hmic_action_items[] = {
    { .label = " None ", .action = ACTION_NONE },
    { .label = " Recorder on/off ", .action = ACTION_RECORDER },
    { .label = " Mute ", .action = ACTION_MUTE },
    { .label = " Step up ", .action = ACTION_STEP_UP },
    { .label = " Step down ", .action = ACTION_STEP_DOWN },
    { .label = " Voice mode ", .action = ACTION_VOICE_MODE },
    { .label = " Battery info ", .action = ACTION_BAT_INFO },
    { .label = " NR toggle ", .action = ACTION_NR_TOGGLE },
    { .label = " NB toggle ", .action = ACTION_NB_TOGGLE },
    { .label = NULL, .action = ACTION_NONE }
};

static void hmic_action_update_cb(lv_event_t * e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    uint8_t     *i = (uint8_t*)lv_event_get_user_data(e);
    uint8_t     val = hmic_action_items[lv_dropdown_get_selected(obj)].action;

    params_lock();

    switch (*i) {
        case 0:
            params.press_f1 = val;
            params_unlock(&params.dirty.press_f1);
            break;

        case 1:
            params.press_f2 = val;
            params_unlock(&params.dirty.press_f2);
            break;

        case 2:
            params.long_f1 = val;
            params_unlock(&params.dirty.long_f1);
            break;

        case 3:
            params.long_f2 = val;
            params_unlock(&params.dirty.long_f2);
            break;
    }
}

static uint8_t make_hmic_action(uint8_t row) {
    struct {const char *label; uint8_t param;} items[] = {
        {"HMic F1 press", params.press_f1},
        {"HMic F2 press", params.press_f2},
        {"HMic F1 long press", params.long_f1},
        {"HMic F2 long press", params.long_f2}
    };
    size_t items_len = sizeof(items) / sizeof(items[0]);
    lv_obj_t    *obj;

    for (uint8_t i = 0; i < items_len; i++) {

        obj = lv_label_create(grid);

        lv_label_set_text(obj, items[i].label);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

        obj = lv_dropdown_create(grid);

        dialog_item(&dialog, obj);

        lv_obj_set_size(obj, SMALL_6, 56);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_center(obj);

        lv_obj_t *list = lv_dropdown_get_list(obj);
        lv_obj_add_style(list, &dialog_dropdown_list_style, 0);

        lv_dropdown_set_symbol(obj, NULL);

        lv_dropdown_clear_options(obj);

        uint8_t n = 0;

        while (hmic_action_items[n].label) {
            lv_dropdown_add_option(obj, hmic_action_items[n].label, LV_DROPDOWN_POS_LAST);

            if (hmic_action_items[n].action == items[i].param) {
                lv_dropdown_set_selected(obj, n);
            }

            n++;
        }

        uint8_t *param = (uint8_t*)malloc(sizeof(uint8_t));
        *param = i;

        lv_obj_add_event_cb(obj, hmic_action_update_cb, LV_EVENT_VALUE_CHANGED, param);

        row++;
    }

    return row;
}

/* Play,Rec gain */

static void play_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = audio_set_play_vol(lv_slider_get_value(obj));

    params_float_set(&params.play_gain_db_f, val);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    radio_set_line_in(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static void rec_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = audio_set_rec_vol(lv_slider_get_value(obj));

    params_float_set(&params.rec_gain_db_f, val);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    radio_set_line_in(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static uint8_t make_audio_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Play,Rec gain");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, params.play_gain_db_f.x, -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f", play_gain_update_cb);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, params.rec_gain_db_f.x, -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f", rec_gain_update_cb);

    return row + 1;
}

/* Transverter */

static void transverter_from_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    cfg_transverter_t   *transverter = (cfg_transverter_t*)lv_event_get_user_data(e);
    subject_set_int(transverter->from.val, lv_spinbox_get_value(obj) * 1000000L);
}

static void transverter_to_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    cfg_transverter_t   *transverter = (cfg_transverter_t*)lv_event_get_user_data(e);
    subject_set_int(transverter->to.val, lv_spinbox_get_value(obj) * 1000000L);
}

static void transverter_shift_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    cfg_transverter_t   *transverter = (cfg_transverter_t*)lv_event_get_user_data(e);
    subject_set_int(transverter->shift.val, lv_spinbox_get_value(obj) * 1000000L);
}

static uint8_t make_transverter(uint8_t row, uint8_t n) {
    lv_obj_t        *obj;
    uint8_t         col = 0;
    cfg_transverter_t   *transverter = &cfg_transverters[n];

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text_fmt(obj, "Transverter %i", n + 1);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* From */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, subject_get_int(transverter->from.val) / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_from_update_cb, LV_EVENT_VALUE_CHANGED, transverter);

    /* To */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, subject_get_int(transverter->to.val) / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_to_update_cb, LV_EVENT_VALUE_CHANGED, transverter);

    /* Shift */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, subject_get_int(transverter->shift.val) / 1000000L);
    lv_spinbox_set_range(obj, 42, 500);
    lv_spinbox_set_digit_format(obj,3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_shift_update_cb, LV_EVENT_VALUE_CHANGED, transverter);

    return row + 1;
}

/* Voice */

static uint8_t make_voice(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Voice mode");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.voice_mode, " Off \n When LCD off \n Always");

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    /* * */

    row++;
    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Voice rate, pitch, volume");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = spinbox_uint8(grid, &params.voice_rate);

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;

    obj = spinbox_uint8(grid, &params.voice_pitch);

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;

    obj = spinbox_uint8(grid, &params.voice_volume);

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;

    return row + 1;
}

/* Spectrum and waterfall auto */

#define AUTO_LEVEL_STEP 0.2f

static void auto_level_offset_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = lv_slider_get_value(obj) * AUTO_LEVEL_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    Subject *subj = (Subject *)lv_event_get_user_data(e);
    subject_set_float(subj, val);
}

static uint8_t make_auto_levels(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Spectrum auto, offset");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* On/off */
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg.auto_level_enabled.val);
    lv_obj_set_width(obj, SMALL_3 - 30);

    /* Offset */
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = slider_with_text(obj, subject_get_float(cfg.auto_level_offset.val), -15.0f, 15.0f, AUTO_LEVEL_STEP,
                           SMALL_3 - 120, "%0.1f", auto_level_offset_update_cb, (void *)cfg.auto_level_offset.val);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);

    return row + 1;
}

static uint8_t make_waterfall_line_zoom(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Waterfall line, zoom");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_center_line);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_set_width(obj, SMALL_3 - 30);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_zoom);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static uint8_t make_waterfall_smooth_scroll(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Waterfall smooth scroll");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_smooth_scroll);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static void sp_mode_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        radio_change_spmode(-1);
    } else {
        radio_change_spmode(1);
    }
}

static uint8_t make_sp_mode(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Speaker mode");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = lv_switch_create(obj);

    dialog_item(&dialog, obj);

    lv_obj_center(obj);
    lv_obj_add_event_cb(obj, sp_mode_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (!params.spmode.x) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* Compressor threshold, makeup */
#define COMP_TH_MAKEUP_STEP 0.5f

static void comp_th_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = lv_slider_get_value(obj) * COMP_TH_MAKEUP_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    Subject *subj = (Subject *)lv_event_get_user_data(e);
    subject_set_float(subj, val);
}


static uint8_t make_comp_th_makeup(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Comp threshold, makeup");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, subject_get_float(cfg.comp_threshold_offset.val),
        -15.0f, 15.0f, COMP_TH_MAKEUP_STEP,
        SMALL_3 - 120, "%0.1f", comp_th_gain_update_cb, (void*)cfg.comp_threshold_offset.val);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, subject_get_float(cfg.comp_makeup_offset.val),
        -15.0f, 15.0f, COMP_TH_MAKEUP_STEP,
        SMALL_3 - 120, "%0.1f", comp_th_gain_update_cb, (void*)cfg.comp_makeup_offset.val);

    return row + 1;
}

/* TX offset */
#define TX_OFFSET_SCALE 50

static void tx_iq_offset_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(obj);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    Subject *subj = (Subject *)lv_event_get_user_data(e);
    subject_set_int(subj, val * TX_OFFSET_SCALE);
}


static uint8_t make_tx_offset(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "TX IQ offsets");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, subject_get_int(cfg.tx_i_offset.val) / TX_OFFSET_SCALE,
        -10000 / TX_OFFSET_SCALE, 10000 / TX_OFFSET_SCALE, 1,
        SMALL_3 - 110, "%d", tx_iq_offset_update_cb, (void*)cfg.tx_i_offset.val);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, subject_get_int(cfg.tx_q_offset.val) / TX_OFFSET_SCALE,
        -10000 / TX_OFFSET_SCALE, 10000 / TX_OFFSET_SCALE, 1,
        SMALL_3 - 110, "%d", tx_iq_offset_update_cb, (void*)cfg.tx_q_offset.val);

    return row + 1;
}

/* Output gain */
#define OUTPUT_GAIN_STEP 0.2f

static void output_gain_subject_cb(Subject * subj, void *user_data) {
    lv_obj_t *slider = (lv_obj_t *)user_data;
    float val = subject_get_float(subj);
    lv_slider_set_value(slider, val / OUTPUT_GAIN_STEP, LV_ANIM_OFF);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(slider);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static void output_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = (float)lv_slider_get_value(obj) * OUTPUT_GAIN_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    subject_set_float(cfg_cur.band->output_gain.val, val);
}

static uint8_t make_output_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Output gain");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_6, 56);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);
    lv_obj_t *slider = slider_with_text(cell, subject_get_float(cfg_cur.band->output_gain.val),
        -25.0f, 25.0f, OUTPUT_GAIN_STEP,
        SMALL_6 - 120, "%0.1f", output_gain_update_cb);

    output_gain_observer = cfg_cur.band->output_gain.val->subscribe_delayed(output_gain_subject_cb, (void*)slider);
    output_gain_observer->notify();

    return row + 1;
}



static uint8_t make_freq_accel(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Freq acceleration");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.freq_accel, " None \n Lite \n Strong");

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

static uint8_t make_theme(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Theme");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.theme, " Simple \n Legacy \n Flat", theme_update_cb);

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

static uint8_t make_delimiter(uint8_t row) {
    row_dsc[row] = 10;

    return row + 1;
}

static void grid_create() {
    std::fill_n(row_dsc, 64, 54);
    grid = lv_obj_create(dialog.obj);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_size(grid, 780, 330);
    lv_obj_set_style_text_color(grid, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, SMALL_PAD, 0);
    lv_obj_set_style_pad_row(grid, 5, 0);

    lv_obj_center(grid);
}

static void grid_delete() {
    if (grid) {
        lv_group_set_editing(keyboard_group, false);
        lv_obj_del(grid);
    }
}

static void make_general_page() {
    grid_delete();
    grid_create();

    uint8_t row = 0;

    now = time(NULL);
    struct tm *t = localtime(&now);

    memcpy(&ts, t, sizeof(ts));

    row = make_date(row);
    row = make_time(row);

    row = make_delimiter(row);
    row = make_backlight(row);

    row = make_delimiter(row);
    row = make_line_gain(row);

    row = make_delimiter(row);
    row = make_audio_gain(row);

    row = make_delimiter(row);
    row = make_voice(row);

    row = make_delimiter(row);
    row = make_sp_mode(row);

    if (x6100_control_get_patched_revision() >= 3) {
        row = make_delimiter(row);
        row = make_comp_th_makeup(row);
    }

    row = make_delimiter(row);
    row = make_tx_offset(row);

    if (x6100_control_get_patched_revision() >= 3) {
        row = make_delimiter(row);
        row = make_output_gain(row);
    }

    row = make_delimiter(row);

    for (uint8_t i = 0; i < TRANSVERTER_NUM; i++)
        row = make_transverter(row, i);

    row_dsc[row] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
}

static void make_ui_page() {
    grid_delete();
    grid_create();
    uint8_t row = 0;

    // row = make_delimiter(row);
    row = make_clock(row);

    row = make_delimiter(row);
    row = make_long_action(row);

    row = make_delimiter(row);
    row = make_hmic_action(row);

    row = make_mag(row);
    row = make_delimiter(row);

    row = make_delimiter(row);
    row = make_waterfall_line_zoom(row);
    row = make_auto_levels(row);
    row = make_waterfall_smooth_scroll(row);

    row = make_delimiter(row);
    row = make_freq_accel(row);

    row = make_delimiter(row);
    row = make_theme(row);


    row_dsc[row] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);
    make_general_page();
    buttons_mark(&btn_ui, false);
    buttons_mark(&btn_general, true);
}

static void destruct_cb() {
    grid_delete();
    grid = NULL;
    if (output_gain_observer) {
        delete output_gain_observer;
    }
}

static void key_cb(lv_event_t * e) {
    uint32_t    key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case HKEY_FINP:
             lv_group_set_editing(keyboard_group, !lv_group_get_editing((const lv_group_t*) keyboard_group));
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
