/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"

#include <stdint.h>
#include <stdbool.h>
#include <liquid/liquid.h>

typedef enum {
    RTTY_OFF = 0,
    RTTY_RX,
    RTTY_TX
} rtty_state_t;

void rtty_init();
void rtty_put_audio_samples(unsigned int n, float *samples);

void rtty_set_state(rtty_state_t state);
rtty_state_t rtty_get_state();

float rtty_change_rate(int16_t df);
uint16_t rtty_change_shift(int16_t df);
uint16_t rtty_change_center(int16_t df);
bool rtty_change_reverse(int16_t df);
