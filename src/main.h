/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "rotary.h"
#include "encoder.h"

#define VERSION "v0.30.2-CM3"

typedef enum {
    VOL_EDIT = 0,
    VOL_SELECT,
} vol_rotary_t;

extern rotary_t     *vol;
extern encoder_t    *mfk;
