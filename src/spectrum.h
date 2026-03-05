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

lv_obj_t * spectrum_init(lv_obj_t * parent);
void spectrum_data(float *data_buf, uint16_t size, bool tx, uint32_t base_freq);
void spectrum_min_max_reset();

void spectrum_update_max(float db);
void spectrum_update_min(float db);
void spectrum_clear();
// void spectrum_update_filters();
// void spectrum_update_factor();
