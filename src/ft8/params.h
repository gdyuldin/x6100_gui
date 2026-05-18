/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 persistent tuning parameters
 *
 *  Copyright (c) 2026
 */

/*
 *  Small plain-text parameter files under /mnt/ tweaked by the user to tune
 *  the FT8 Auto-DNF detector without recompiling. Each file holds one float.
 *
 *  Files created with defaults on first boot; values read back and clipped
 *  to sane ranges on every dialog entry. Out-of-range values are rewritten
 *  with the default so the file stays self-describing.
 *
 *      /mnt/auto_dnf_scan_start_sec.txt   (default 0.25)
 *      /mnt/auto_dnf_scan_end_sec.txt     (default 0.75)
 *      /mnt/auto_dnf_clear_time_sec.txt   (default 0.25)
 *      /mnt/auto_notch_threshold_db.txt   (default 40.0)
 */

#pragma once

#include <stdbool.h>

/* Compiled-in defaults mirrored to disk if the file is absent. */
#define FT8_AUTO_DNF_SCAN_START_SEC_DEFAULT  0.25f
#define FT8_AUTO_DNF_SCAN_END_SEC_DEFAULT    0.75f
#define FT8_AUTO_DNF_CLEAR_TIME_SEC_DEFAULT  0.25f
#define FT8_AUTO_DNF_MIN_DELTA_DB_DEFAULT    40.0f

typedef struct {
    float scan_start_sec;   /* slot_start-relative scan window begin */
    float scan_end_sec;     /* slot_start-relative scan window end */
    float clear_time_sec;   /* slot_start-relative clear moment */
    float min_delta_db;     /* minimum peak prominence to notch */
} ft8_tuning_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Load all four parameters from disk; missing/invalid values rewritten with
 * defaults. Always populates every field in *out. */
void ft8_params_load(ft8_tuning_t *out);

#ifdef __cplusplus
}
#endif
