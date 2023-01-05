/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>

uint64_t get_time();
void split_freq(uint64_t freq, uint16_t *mhz, uint16_t *khz, uint16_t *hz);
int32_t align_int(int32_t x, uint16_t step);
