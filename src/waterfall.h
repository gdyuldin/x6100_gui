/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <unistd.h>
#include <stdint.h>

#include "lvgl/lvgl.h"
#include "dsp.h"

/* Waterfall consumes the full-resolution, undecimated transform. */
#define WATERFALL_NFFT DSP_MAX_NFFT

lv_obj_t * waterfall_init(lv_obj_t * overlay_parent, lv_coord_t y, lv_coord_t h);

/* Reposition/resize the waterfall strip. Rows are preallocated for the maximum
 * height, so no reallocation happens here. */
void waterfall_set_geometry(lv_coord_t y, lv_coord_t h);
void waterfall_data(const float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint32_t width_hz, float min, float max);

/* Pause/resume waterfall frame delivery (e.g. while FT8 owns the screen). The
 * DSP keeps running the FFT, S-meter and noise floor. */
void waterfall_set_enabled(bool enabled);

/* Direct-render entry point. Call from the main loop between lv_timer_handler()
 * and drm_flip(). Renders the waterfall into the DRM primary back-buffer when new
 * data arrived or the render conditions changed. Returns true if it rendered. */
bool waterfall_process(void);
