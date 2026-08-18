/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <unistd.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * band_info_init(lv_obj_t *parent);
void band_info_update(int32_t f);

#ifdef __cplusplus
}
#endif