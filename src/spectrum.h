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
#include <stdbool.h>

#include "lvgl/lvgl.h"

lv_obj_t *spectrum_init(lv_obj_t *overlay_parent, lv_coord_t y, lv_coord_t h);
void      spectrum_data(const float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint8_t fft_dec, float min, float max);
void      spectrum_clear();

/* Direct-render entry point. Call from the main loop between lv_timer_handler()
 * and drm_flip(). Renders the spectrum into the DRM primary back-buffer when new
 * data arrived or the render conditions changed. Returns true if it rendered. */
bool spectrum_process(void);
// void spectrum_update_filters();
// void spectrum_update_factor();
