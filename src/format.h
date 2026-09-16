/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <aether_radio/x6100_control/control.h>

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Small UI label helpers for enumerated radio settings. Pure functions; the
// returned string is either a static literal or (for the compressor) a shared
// static buffer valid until the next call.

char *format_mic_str_get(x6100_mic_sel_t val);

char *format_key_mode_str_get(x6100_key_mode_t val);

char *format_iambic_mode_str_get(x6100_iambic_mode_t val);

char *format_comp_str_get(uint8_t comp);

#ifdef __cplusplus
}
#endif
