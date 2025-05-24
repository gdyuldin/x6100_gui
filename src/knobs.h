/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 */

#pragma once

#include "lvgl/lvgl.h"

#include <unistd.h>

#define KNOBS_HEIGHT 30

lv_obj_t * knobs_init(lv_obj_t * parent);
void knobs_update_vol(const char * fmt, ...);
void knobs_update_mfk(const char * fmt, ...);