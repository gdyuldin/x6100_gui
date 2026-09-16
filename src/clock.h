/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "lvgl/lvgl.h"

#include <stdint.h>

#include "settings_types.h"

lv_obj_t * clock_init(lv_obj_t * parent);

void clock_set_view(clock_view_t x);
void clock_set_time_timeout(uint8_t sec);
void clock_set_power_timeout(uint8_t sec);
void clock_set_tx_timeout(uint8_t sec);

void clock_say_bat_info();
