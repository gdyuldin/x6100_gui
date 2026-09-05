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

#ifdef __cplusplus
extern "C" {
#endif

void indicators_init(lv_obj_t * parent, lv_coord_t h, lv_coord_t meter_w);

// Hide/show RX block of indicators
void indicators_left_show(bool v);

void indicators_show(bool v);

// const char* info_params_mode_label_get();
// const char* info_params_agc();
// const char* info_params_vfo_label_get();

// void info_lock_mode(bool lock);

#ifdef __cplusplus
}
#endif
