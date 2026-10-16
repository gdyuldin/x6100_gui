/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (c) 2026 Krzysztof Myszkowski SP2KMO
 *
 *  Xiegu X6100 LVGL GUI - RTTY button pages 2..6
 *
 *  Page 1 (Rate, Shift, Center, Reverse) stays in buttons.cpp. The pages
 *  added here are chained after it through buttons_group_rtty:
 *
 *    2  Free Text | Stop TX | New Macro | SQL (threshold and live level)
 *    3  macros 1-4      4  macros 5-8      5  macros 9-12
 *    6  DX call | RST sent | RST received | Auto DX
 *
 *  Macro buttons: press sends, hold edits (an empty slot creates one).
 */

#pragma once

#include "buttons.h"

#ifdef __cplusplus
extern "C" {
#endif

extern buttons_page_t buttons_page_rtty_2;
extern buttons_page_t buttons_page_rtty_3;
extern buttons_page_t buttons_page_rtty_4;
extern buttons_page_t buttons_page_rtty_5;
extern buttons_page_t buttons_page_rtty_6;

/* Copy the macro names into the button captions; empty slots show "--". */
void rtty_refresh_macro_labels(void);

/* The SQL caption shows a live level, so it is repainted on a timer
 * while the RTTY application is open. */
void rtty_sql_timer_start(void);
void rtty_sql_timer_stop(void);

#ifdef __cplusplus
}
#endif
