/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>

#define FT8_SYMBOL_BT   2.0f
#define FT4_SYMBOL_BT   1.0f

int16_t * gfsk_synth(const uint8_t *symbols, uint16_t n_sym, float f0, float symbol_bt, float symbol_period, uint32_t *n_samples);
