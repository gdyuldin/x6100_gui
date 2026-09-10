/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include "encoder.h"
#include "keyboard.h"
#include "display.h"

static void encoder_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    struct input_event  in;
    encoder_t           *encoder = (encoder_t*) drv->user_data;
    int32_t             diff = 0;
    bool                send = false;

    while (read(encoder->fd, &in, sizeof(struct input_event)) > 0) {
        if (in.type == EV_REL) {
            diff += in.value;
            send = true;
        }
    }

    if (send) {
        display_tick();
    }

    data->enc_diff = -diff;
    data->state = encoder->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

encoder_t * encoder_init(char *dev_name) {
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd == -1) {
        perror("unable to open encoder interface:");

        return NULL;
    }

    fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK);

    encoder_t *encoder = malloc(sizeof(encoder_t));

    memset(encoder, 0, sizeof(encoder_t));
    encoder->fd = fd;

    lv_indev_drv_init(&encoder->indev_drv);

    encoder->indev_drv.type = LV_INDEV_TYPE_ENCODER;
    encoder->indev_drv.read_cb = encoder_input_read;
    encoder->indev_drv.user_data = encoder;

    encoder->indev = lv_indev_drv_register(&encoder->indev_drv);

    lv_indev_set_group(encoder->indev, keyboard_group);

    return encoder;
}
