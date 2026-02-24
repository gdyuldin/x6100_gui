/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "cfg/subjects.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cfg/cfg.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MFK_STATE_EDIT = 0,
    MFK_STATE_SELECT
} mfk_state_t;

extern mfk_state_t  mfk_state;

void mfk_init();

void mfk_update(int16_t diff);
void mfk_change_ctrl(int16_t dir);
void mfk_set_ctrl(cfg_ctrl_t mode);

#ifdef __cplusplus
}
#endif
