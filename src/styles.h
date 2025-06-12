/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "params/params.h"

#include <unistd.h>
#include "lvgl/lvgl.h"

// PATH
#define PATH "A:/dev/shm/"

// COLORS

#define COLOR_LIGHT_GREEN (0x80FF80)
#define COLOR_LIGHT_YELLOW (0xFFFF30)
#define COLOR_LIGHT_RED (0xFF8080)

// Symbols

#define SYMBOL_PLUG "\xEF\x87\xA6"  // F1E6
#define SYMBOL_PLUG_CHARGE "\xEE\x95\x9B"  // E55B

// Pallete

extern const uint32_t *wf_palette;

extern lv_color_t   bg_color;

extern lv_style_t   background_style;
extern lv_style_t   spectrum_style;
extern lv_style_t   freq_style;
extern lv_style_t   freq_main_style;
extern lv_style_t   waterfall_style;
extern lv_style_t   btn_style;
extern lv_style_t   btn_active_style;
extern lv_style_t   btn_disabled_style;
extern lv_style_t   msg_style;
extern lv_style_t   msg_tiny_style;
extern lv_style_t   clock_style;
extern lv_style_t   knobs_style;
extern lv_style_t   info_style;
extern lv_style_t   info_row_style;
extern lv_style_t   info_item_style;
extern lv_style_t   meter_style;
extern lv_style_t   tx_info_style;

extern lv_style_t   panel_top_style;
extern lv_style_t   panel_mid_style;
extern lv_style_t   panel_bottom_style;
extern lv_style_t   pannel_style;

extern lv_style_t   dialog_style;
extern lv_style_t   dialog_item_style;
extern lv_style_t   dialog_item_focus_style;
extern lv_style_t   dialog_item_edited_style;
extern lv_style_t   dialog_dropdown_list_style;

extern lv_style_t   cw_tune_style;

extern lv_font_t    sony_14;
extern lv_font_t    sony_16;
extern lv_font_t    sony_18;
extern lv_font_t    sony_20;
extern lv_font_t    sony_22;
extern lv_font_t    sony_24;
extern lv_font_t    sony_26;
extern lv_font_t    sony_28;
extern lv_font_t    sony_30;
extern lv_font_t    sony_32;
extern lv_font_t    sony_34;
extern lv_font_t    sony_36;
extern lv_font_t    sony_38;
extern lv_font_t    sony_40;
extern lv_font_t    sony_42;
extern lv_font_t    sony_44;
extern lv_font_t    sony_60;

void styles_init(themes_t theme);

void styles_set_theme(themes_t theme);
