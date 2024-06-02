/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include "rotary.h"
#include "keyboard.h"
#include "backlight.h"

static int32_t remain_diff = 0;
static lv_indev_state_t prev_state;


static void rotary_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    struct input_event  in;
    rotary_t            *rotary = (rotary_t*) drv->user_data;
    int32_t             diff = 0;
    bool                send = false;

    if (remain_diff == 0) {
        while (read(rotary->fd, &in, sizeof(struct input_event)) > 0) {
            if (in.type == EV_REL) {
                diff += in.value;
                send = true;
            }
        }

        if (send) {
            backlight_tick();

            if (rotary->left[0] == 0 && rotary->right[0] == 0) {
                lv_event_send(lv_scr_act(), EVENT_ROTARY, (void *) diff);
            } else {
                data->continue_reading = 1;
                remain_diff = diff;
            }
        }
    } else if (prev_state == LV_INDEV_STATE_PRESSED) {
        data->state = LV_INDEV_STATE_RELEASED;
        if (remain_diff != 0) {
            data->continue_reading = 1;
        }
    } else {
        if (remain_diff > 0) {
            diff = 1;
        } else {
            diff = -1;
        }
        remain_diff -= diff;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = diff > 0 ? rotary->left[rotary->mode] : rotary->right[rotary->mode];
        data->continue_reading = 1;

    }
    prev_state = data->state;

}

rotary_t * rotary_init(char *dev_name) {
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd == -1) {
        LV_LOG_ERROR("unable to open rotary interface:");

        return NULL;
    }

    fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK);

    rotary_t *rotary = malloc(sizeof(rotary_t));

    memset(rotary, 0, sizeof(rotary_t));
    rotary->fd = fd;

    lv_indev_drv_init(&rotary->indev_drv);

    rotary->indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    rotary->indev_drv.read_cb = rotary_input_read;
    rotary->indev_drv.user_data = rotary;

    rotary->indev = lv_indev_drv_register(&rotary->indev_drv);

    lv_indev_set_group(rotary->indev, keyboard_group);

    return rotary;
}
