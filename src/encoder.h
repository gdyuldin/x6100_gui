/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include "lvgl/lvgl.h"
#include "events.h"


typedef enum {
    ENC_STATE_EDIT = 0,
    ENC_STATE_SELECT
} enc_state_t;

typedef void (*enc_update_fn_t)(int16_t diff, bool voice, enc_state_t state);
typedef void (*enc_update_mode_fn_t)(int16_t dir, enc_state_t state);
typedef void (*enc_set_mode_fn)(void *);

typedef struct {
    int                     fd;
    bool                    pressed;
    
    lv_indev_drv_t          indev_drv;
    lv_indev_t              *indev;
    uint32_t                event_id;

    enc_state_t             state;
    enc_update_fn_t         update_fn;
    enc_update_mode_fn_t    update_mode_fn;
    enc_set_mode_fn         set_mode_fn;
} encoder_t;

encoder_t * encoder_init(char *dev_name, uint32_t event_id);

void encoder_state_toggle(encoder_t * enc);

void encoder_update(encoder_t * enc, int16_t diff, bool voice);

void encoder_set_mode(encoder_t * enc, void *mode);

// void encoder_update_mode(encoder_t * enc, int16_t dir);
