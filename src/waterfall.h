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

void waterfall_init(lv_obj_t * overlay_parent, lv_coord_t y, lv_coord_t h);
void waterfall_data(float *data_buf, uint16_t size, bool tx, uint32_t base_freq, uint32_t width_hz);
void waterfall_min_max_reset();

void waterfall_update_max(float db);
void waterfall_update_min(float db);
void waterfall_refresh_reset();
void waterfall_refresh_period_set(uint8_t k);

/* Direct-render entry point. Call from the main loop between lv_timer_handler()
 * and drm_flip(). Renders the waterfall into the DRM primary back-buffer when new
 * data arrived or the render conditions changed. Returns true if it rendered. */
bool waterfall_process(void);
