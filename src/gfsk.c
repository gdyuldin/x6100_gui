/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Based on gcFT8 de F4HTB
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <math.h>
#include <stdlib.h>
#include "gfsk.h"
#include "audio.h"

#define GFSK_CONST_K    5.336446f

static void gfsk_pulse(uint16_t n_spsym, float symbol_bt, float *pulse) {
    for (uint32_t i = 0; i < 3 * n_spsym; i++) {
        float t = i / (float)n_spsym - 1.5f;
        float arg1 = GFSK_CONST_K * symbol_bt * (t + 0.5f);
        float arg2 = GFSK_CONST_K * symbol_bt * (t - 0.5f);

        pulse[i] = (erff(arg1) - erff(arg2)) / 2;
    }
}

int16_t * gfsk_synth(const uint8_t *symbols, uint16_t n_sym, float f0, float symbol_bt, float symbol_period, uint32_t *n_samples) {
    uint32_t    n_spsym = (uint32_t)(0.5f + AUDIO_PLAY_RATE * symbol_period);  /* Samples per symbol */
    uint32_t    n_wave = n_sym * n_spsym;                                      /* Number of output samples */
    float       hmod = 1.0f;
    float       dphi_peak = 2 * M_PI * hmod / n_spsym;
    float       dphi[n_wave + 2 * n_spsym];
    int16_t     *samples = malloc(sizeof(int16_t) * n_wave);

    *n_samples = n_wave;

    /* Shift frequency up by f0 */

    for (uint32_t i = 0; i < n_wave + 2 * n_spsym; i++) {
        dphi[i] = 2 * M_PI * f0 / AUDIO_PLAY_RATE;
    }

    float pulse[3 * n_spsym];

    gfsk_pulse(n_spsym, symbol_bt, pulse);

    for (uint32_t i = 0; i < n_sym; i++) {
        int ib = i * n_spsym;

        for (uint32_t j = 0; j < 3 * n_spsym; j++) {
            dphi[j + ib] += dphi_peak * symbols[i] * pulse[j];
        }
    }

    /* Add dummy symbols at beginning and end with tone values equal to 1st and last symbol, respectively */

    for (uint32_t j = 0; j < 2 * n_spsym; j++) {
        dphi[j] += dphi_peak * pulse[j + n_spsym] * symbols[0];
        dphi[j + n_sym * n_spsym] += dphi_peak * pulse[j] * symbols[n_sym - 1];
    }

    /* Calculate and insert the audio waveform */

    float phi = 0;

    for (uint32_t k = 0; k < n_wave; k++) { /* Don't include dummy symbols */
        samples[k] = sinf(phi) * 32767.0f * 0.8f;
        phi = fmodf(phi + dphi[k + n_spsym], 2 * M_PI);
    }

    /* Apply envelope shaping to the first and last symbols */

    int n_ramp = n_spsym / 8;

    for (uint32_t i = 0; i < n_ramp; i++) {
        float env = (1 - cosf(2 * M_PI * i / (2 * n_ramp))) / 2;

        samples[i] *= env;
        samples[n_wave - 1 - i] *= env;
    }

    return samples;
}
