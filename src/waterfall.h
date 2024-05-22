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

extern float waterfall_auto_min;
extern float waterfall_auto_max;

lv_obj_t * waterfall_init(lv_obj_t * parent);
void waterfall_data(float *data_buf, uint16_t size);
void waterfall_set_height(lv_coord_t h);
void waterfall_clear();
void waterfall_band_set();

void waterfall_change_max(int16_t d);
void waterfall_change_min(int16_t d);
void waterfall_change_freq(int64_t df);
void waterfall_update_band(uint64_t f);
