/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Applies pending schema/data migrations to the already-opened cfg database
// (cfg_db_open). Uses the cfg connection accessor; returns 0 on success.
int migrations_apply(void);

#ifdef __cplusplus
}
#endif
