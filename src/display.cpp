/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "util.h"
#include "voice.h"
#include "cfg/cfg_api.h"

extern "C" {
    #include <aether_radio/x6100_control/low/gpio.h>
}


#define SYSFS_PATH "/sys/bus/spi/devices/spi0.0/immed_msg"
#define INVERT_ON_CMD "21\n"
#define INVERT_OFF_CMD "20\n"

static int          power;
static int          brightness;
static bool         on = true;

static lv_timer_t   *timer = NULL;

static Subscription display_invert_obs_;

static void on_display_invert_change(Subject * /*subj*/, void * /*user_data*/) {
    display_invert(cfg.display.invert()->get() != 0);
}

static void display_timer(lv_timer_t *t) {
    display_set_brightness(cfg.display.brightness_idle()->get());
    x6100_gpio_set(x6100_pin_light, cfg.display.brightness_buttons()->get() == BUTTONS_LIGHT ? 1 : 0);
    timer = NULL;
}

static void set_brightness(int16_t value) {
    if (brightness > 0) {
        char    str[8];
        int     len = snprintf(str, sizeof(str), "%i\n", 10 - value);

        ssize_t n = write(brightness, str, len);
    }
}

static void set_power(bool value) {
    if (power > 0) {
        char    str[8];
        int     len = snprintf(str, sizeof(str), "%i\n", value ? 0 : 1);

        ssize_t n = write(power, str, len);
    }
}

void display_init() {
    power = open("/sys/class/backlight/backlight/bl_power", O_WRONLY);
    brightness = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
    on = true;

    display_tick();

    display_invert(cfg.display.invert()->get() != 0);
    display_invert_obs_ = Subscription(cfg.display.invert()->subscribe(on_display_invert_change));
}

void display_tick() {
    if (timer) {
        lv_timer_set_period(timer, cfg.display.brightness_timeout()->get() * 1000);
        lv_timer_reset(timer);
    } else {
        timer = lv_timer_create(display_timer, cfg.display.brightness_timeout()->get() * 1000, NULL);
        lv_timer_set_repeat_count(timer, 1);

        display_set_brightness(cfg.display.brightness_normal()->get());
        x6100_gpio_set(x6100_pin_light, cfg.display.brightness_buttons()->get() == BUTTONS_DARK ? 0 : 1);
    }
}

void display_set_brightness(int16_t value) {
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

void display_set_buttons_backlight(buttons_light_t value) {
    cfg.display.brightness_buttons()->set(value);

    x6100_gpio_set(x6100_pin_light, value == BUTTONS_DARK ? 0 : 1);
}

void display_power_toggle() {
    if (on) {
        set_power(false);
        set_brightness(9);
        x6100_gpio_set(x6100_pin_light, 0);

        on = false;
        voice_say_text_fmt("Display off");
        lv_disp_enable_invalidation(lv_disp_get_default(), false);
    } else {
        lv_disp_enable_invalidation(lv_disp_get_default(), true);
        set_power(true);
        set_brightness(cfg.display.brightness_normal()->get());
        x6100_gpio_set(x6100_pin_light, cfg.display.brightness_buttons()->get() == BUTTONS_DARK ? 0 : 1);

        voice_say_text_fmt("Display on");
        on = true;
    }
}

bool display_is_on() {
    return on;
}

void display_invert(bool on) {
    FILE *fp = fopen(SYSFS_PATH, "w");
    if (fp != NULL) {
        const char *cmd;
        if (on) {
            cmd = INVERT_ON_CMD;
        } else {
            cmd = INVERT_OFF_CMD;
        }
        fprintf(fp, cmd);
        fclose(fp);
    } else {
        LV_LOG_ERROR("%s not found", SYSFS_PATH);
    }
}
