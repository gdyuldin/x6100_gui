/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "format.h"

#include <stdio.h>

char *format_mic_str_get(x6100_mic_sel_t val) {
    switch (val) {
        case x6100_mic_builtin:
            return "Built-In";
        case x6100_mic_handle:
            return "Handle";
        case x6100_mic_auto:
            return "Auto";
        default:
            return "";
    }
}

char *format_key_mode_str_get(x6100_key_mode_t val) {
    switch (val) {
        case x6100_key_manual:
            return "Manual";
        case x6100_key_auto_left:
            return "Auto-L";
        case x6100_key_auto_right:
            return "Auto-R";
    }
}

char *format_iambic_mode_str_get(x6100_iambic_mode_t val) {
    switch (val) {
        case x6100_iambic_a:
            return "A";
        case x6100_iambic_b:
            return "B";
    }
}

char *format_comp_str_get(uint8_t comp) {
    static char buf[8];

    if (comp == 1) {
        return "1:1 (Off)";
    }
    sprintf(buf, "%d:1", comp);
    return buf;
}
