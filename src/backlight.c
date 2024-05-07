/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <aether_radio/x6100_control/low/gpio.h>

#include "lvgl/lvgl.h"
#include "backlight.h"
#include "util.h"
#include "voice.h"

static int          power;
static int          brightness;
static bool         on = true;

static lv_timer_t   *timer = NULL;

static void backlight_timer(lv_timer_t *t) {
    backlight_set_brightness(params.brightness_idle);
    x6100_gpio_set(x6100_pin_light, params.brightness_buttons == BUTTONS_LIGHT ? 1 : 0);
    timer = NULL;
}

static void set_brightness(int16_t value) {
    if (brightness > 0) {
        char    str[8];
        int     len = snprintf(str, sizeof(str), "%i\n", 10 - value);

        ssize_t nbytes = write(brightness, str, len);
    }
}

static void set_power(bool value) {
    if (power > 0) {
        char    str[8];
        int     len = snprintf(str, sizeof(str), "%i\n", value ? 0 : 1);
        
        ssize_t nbytes = write(power, str, len);
    }
}

void backlight_init() {
    power = open("/sys/class/backlight/backlight/bl_power", O_WRONLY);
    brightness = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
    on = true;
    
    backlight_tick();
}

void backlight_tick() {
    if (timer) {
        lv_timer_set_period(timer, params.brightness_timeout * 1000);
        lv_timer_reset(timer);
    } else {
        timer = lv_timer_create(backlight_timer, params.brightness_timeout * 1000, NULL);
        lv_timer_set_repeat_count(timer, 1);

        backlight_set_brightness(params.brightness_normal);
        x6100_gpio_set(x6100_pin_light, params.brightness_buttons == BUTTONS_DARK ? 0 : 1);
    }
}

void backlight_set_brightness(int16_t value) {
    if (on) {
        if (value < 0) {
            set_power(false);
            
            /* Setting max PWM for reduce noice */
            set_brightness(9);
        } else {
            set_brightness(value);
            set_power(true);
        }
    }
}

void backlight_set_buttons(buttons_light_t value) {
    params_lock();
    params.brightness_buttons = value;
    params_unlock(&params.durty.brightness_buttons);

    x6100_gpio_set(x6100_pin_light, value == BUTTONS_DARK ? 0 : 1);
}

void backlight_switch() {
    if (on) {
        set_power(false);
        set_brightness(9);
        x6100_gpio_set(x6100_pin_light, 0);

        on = false;
        voice_say_text_fmt("Display off");
    } else {
        set_power(true);
        set_brightness(params.brightness_normal);
        x6100_gpio_set(x6100_pin_light, params.brightness_buttons == BUTTONS_DARK ? 0 : 1);

        voice_say_text_fmt("Display on");
        on = true;
    }
}

bool backlight_is_on() {
    return on;
}
