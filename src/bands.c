/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "lvgl/lvgl.h"
#include "bands.h"
#include "params.h"
#include "radio.h"
#include "info.h"
#include "spectrum.h"
#include "waterfall.h"
#include "main_screen.h"
#include "pannel.h"
#include "voice.h"
#include "dsp.h"

void bands_activate(band_t *band, uint64_t *freq) {
    params_lock();
    params_band_save();
    params.band = band->id;
    params_band_load();
    params_unlock(&params.durty.band);

    if (freq) {
        params_band_freq_set(*freq);
    }

    radio_vfo_set();
    radio_mode_set();
    spectrum_mode_set();
    spectrum_band_set();
    waterfall_band_set();
}

void bands_change(bool up) {
    band_t band = { .name = NULL };
    
    if (params_bands_find_next(params_band.vfo_x[params_band.vfo].freq, up, &band)) {
        bands_activate(&band, NULL);
        radio_load_atu();
        info_params_set();
        pannel_visible();

        waterfall_clear();
        spectrum_clear();
        dsp_auto_clear();
        main_screen_band_set();
            
        voice_say_text_fmt("Band %s", band.name);
    }
}
