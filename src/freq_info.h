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

lv_obj_t * freq_info_init(lv_obj_t * parent);

// const char* info_params_mode_label_get();
// const char* info_params_agc();
// const char* info_params_vfo_label_get();

// void info_lock_mode(bool lock);

#ifdef __cplusplus
}
#endif
