/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "clock.h"

#include "styles.h"
#include "radio.h"
#include "util.h"
#include "backlight.h"
#include "voice.h"
#include "msg.h"

#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>

typedef enum {
    CLOCK_TIME = 0,
    CLOCK_POWER
} clock_state_t;

static lv_obj_t         *obj;
static pthread_mutex_t  power_mux;

static clock_state_t    state = CLOCK_TIME;
static uint64_t         timeout;

static lv_timer_t       *timer_time_sync;
static time_t           last_time_sync=0;

static float            v_ext;
static float            v_bat;
static uint8_t          cap_bat;
static bool             charging;

static char             str[32];

static void check_time_sync_cb(lv_timer_t * t);
static time_t get_last_sync_time();

static void set_state(clock_state_t new_state) {
    state = new_state;

    switch (state) {
        case CLOCK_TIME:
            lv_obj_set_style_text_font(obj, &sony_38, 0);
            lv_obj_set_style_pad_ver(obj, 18, 0);
            break;

        case CLOCK_POWER:
            lv_obj_set_style_text_font(obj, &sony_30, 0);
            lv_obj_set_style_pad_ver(obj, 5, 0);
            break;
    }
}

static void show_time() {
    time_t      now;
    struct tm   *t;

    if (params.clock_view == CLOCK_TIME_ALLWAYS) {
        set_state(CLOCK_TIME);
    } else if (params.clock_view == CLOCK_POWER_ALLWAYS) {
        set_state(CLOCK_POWER);
    } else if (params.clock_view == CLOCK_TIME_POWER) {
        uint64_t    ms = get_time();

        if (radio_get_state() == RADIO_RX) {
            if (ms > timeout) {
                switch (state) {
                    case CLOCK_TIME:
                        set_state(CLOCK_POWER);
                        timeout = ms + params.clock_power_timeout * 1000;
                        break;

                    case CLOCK_POWER:
                        set_state(CLOCK_TIME);
                        timeout = ms + params.clock_time_timeout * 1000;
                        break;
                }
            }
        } else {
            set_state(CLOCK_POWER);
            timeout = ms + params.clock_tx_timeout * 1000;
        }
    }

    switch (state) {
        case CLOCK_TIME:
            now = time(NULL);
            t = localtime(&now);

            snprintf(str, sizeof(str), "%02i:%02i:%02i", t->tm_hour, t->tm_min, t->tm_sec);
            break;

        case CLOCK_POWER:
            pthread_mutex_lock(&power_mux);
            const char * bat_sym;
            const char * ext_sym;
            switch ((uint8_t)(v_bat * 10)) {
                case 0 ... 63:
                    bat_sym = LV_SYMBOL_BATTERY_EMPTY;
                    break;
                case 64 ... 69:
                    bat_sym = LV_SYMBOL_BATTERY_1;
                    break;
                case 70 ... 75:
                    bat_sym = LV_SYMBOL_BATTERY_2;
                    break;
                case 76 ... 81:
                    bat_sym = LV_SYMBOL_BATTERY_3;
                    break;
                case 82 ... 100:
                    bat_sym = LV_SYMBOL_BATTERY_FULL;
                    break;
            }
            if (charging) {
                ext_sym = SYMBOL_PLUG_CHARGE;
            } else {
                ext_sym = SYMBOL_PLUG;
            }

            if (v_ext < 3.0f) {
                snprintf(str, sizeof(str), "%s %.1fv\n%i%%", bat_sym, v_bat, cap_bat);
            } else {
                snprintf(str, sizeof(str), "%s %.1fv\n%s %.1fv", bat_sym, v_bat, ext_sym, v_ext);
            }

            pthread_mutex_unlock(&power_mux);
            break;
    }

    lv_label_set_text(obj, str);
}

lv_obj_t * clock_init(lv_obj_t * parent) {
    pthread_mutex_init(&power_mux, NULL);

    obj = lv_label_create(parent);

    lv_obj_add_style(obj, &clock_style, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);

    set_state(CLOCK_TIME);
    timeout = get_time() + params.clock_time_timeout * 1000;

    show_time();
    lv_timer_create(show_time, 500, NULL);

    last_time_sync = get_last_sync_time();
    timer_time_sync = lv_timer_create(check_time_sync_cb, 200, NULL);
}

void clock_update_power(float ext, float bat, uint8_t cap, bool charge_flag) {
    pthread_mutex_lock(&power_mux);
    v_ext = ext;
    v_bat = bat;
    cap_bat = cap;
    charging = charge_flag;
    pthread_mutex_unlock(&power_mux);
}

void clock_set_view(clock_view_t x) {
    params_lock();
    params.clock_view = x;
    params_unlock(&params.dirty.clock_view);
    timeout = get_time();
}

void clock_set_time_timeout(uint8_t sec) {
    params_lock();
    params.clock_time_timeout = sec;
    params_unlock(&params.dirty.clock_time_timeout);
    timeout = get_time();
}

void clock_set_power_timeout(uint8_t sec) {
    params_lock();
    params.clock_power_timeout = sec;
    params_unlock(&params.dirty.clock_power_timeout);
    timeout = get_time();
}

void clock_set_tx_timeout(uint8_t sec) {
    params_lock();
    params.clock_tx_timeout = sec;
    params_unlock(&params.dirty.clock_tx_timeout);
    timeout = get_time();
}

void clock_say_bat_info() {
    voice_sure();
    voice_say_float("Battery voltage|", v_bat);
}

static void check_time_sync_cb(lv_timer_t * t) {
    time_t new_time = get_last_sync_time();
    if (last_time_sync != new_time) {
        last_time_sync = new_time;
        msg_schedule_text_fmt("Time is synchronized with NTP");
    }
}

static time_t get_last_sync_time() {
    struct stat attr;
    if (stat("/var/run/time-sync", &attr) == 0) {
        return attr.st_atime;
    }
    return 0;
}
