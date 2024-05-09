/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>

typedef enum {
    MFK_MIN_LEVEL = 0,
    MFK_MAX_LEVEL,
    MFK_SPECTRUM_FACTOR,
    MFK_SPECTRUM_BETA,
    MFK_PEAK_HOLD,
    MFK_PEAK_SPEED,

    MFK_SPECTRUM_FILL,
    MFK_SPECTRUM_PEAK,
    MFK_CHARGER,

    MFK_KEY_SPEED,
    MFK_KEY_MODE,
    MFK_IAMBIC_MODE,
    MFK_KEY_TONE,
    MFK_KEY_VOL,
    MFK_KEY_TRAIN,
    MFK_QSK_TIME,
    MFK_KEY_RATIO,

    MFK_DNF,
    MFK_DNF_CENTER,
    MFK_DNF_WIDTH,
    MFK_NB,
    MFK_NB_LEVEL,
    MFK_NB_WIDTH,
    MFK_NR,
    MFK_NR_LEVEL,
    
    MFK_AGC_HANG,
    MFK_AGC_KNEE,
    MFK_AGC_SLOPE,
    
    MFK_CW_DECODER,
    MFK_CW_DECODER_SNR,
    MFK_CW_DECODER_PEAK_BETA,
    MFK_CW_DECODER_NOISE_BETA,

    MFK_ANT,
    MFK_RIT,
    MFK_XIT,

    MFK_LAST,

    /* APPs */

    MFK_RTTY_RATE,
    MFK_RTTY_SHIFT,
    MFK_RTTY_CENTER,
    MFK_RTTY_REVERSE,
} mfk_mode_t;

typedef enum {
    MFK_STATE_EDIT = 0,
    MFK_STATE_SELECT
} mfk_state_t;

extern mfk_state_t  mfk_state;

void mfk_update(int16_t diff, bool voice);
void mfk_change_mode(int16_t dir);
void mfk_set_mode(mfk_mode_t mode);
