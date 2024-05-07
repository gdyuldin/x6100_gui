/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "encoder.h"

typedef enum {
    VOL_VOL = 0,
    VOL_SQL,
    VOL_RFG,
    VOL_FILTER_LOW,
    VOL_FILTER_HIGH,
    VOL_PWR,
    VOL_HMIC,
    VOL_MIC,
    VOL_IMIC,
    VOL_MONI,
    VOL_SPMODE,
    VOL_VOICE_LANG,
    VOL_VOICE_RATE,
    VOL_VOICE_PITCH,
    VOL_VOICE_VOLUME,
    
    VOL_LAST
} vol_mode_t;

void vol_init(encoder_t *enc);
void vol_update(int16_t diff, bool voice, enc_state_t state);
void vol_update_mode(int16_t dir, enc_state_t state);
void vol_set_mode(void *mode);
