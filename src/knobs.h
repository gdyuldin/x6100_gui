/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 *  Copyright (c) 2025 Georgy Dyuldin R2RFE
 */

#pragma once

#include "cfg/subjects.h"
#include "cfg/cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

#include <unistd.h>


void knobs_init(lv_obj_t * parent);
void knobs_set_vol_state(bool edit);
void knobs_set_mfk_state(bool edit);
void knobs_display(bool on);
bool knobs_visible();

#ifdef __cplusplus
}

/* C++ only part */
void knobs_set_vol_param(cfg_ctrl_t control);

void knobs_set_mfk_param(cfg_ctrl_t control);
#endif
