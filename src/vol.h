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
    VOL_STATE_EDIT = 0,
    VOL_STATE_SELECT,
} vol_state_t;

void vol_init();

void vol_update(int16_t diff);
void vol_change_ctrl(int16_t dir);
void vol_set_ctrl(cfg_ctrl_t mode);

#ifdef __cplusplus
}
#endif
