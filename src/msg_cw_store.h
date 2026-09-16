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

// Access to the `msg_cw` table (CW message presets). The settings connection
// is owned by cfg (cfg_db_get), so cfg_init() must have run before any call.
// This module is UI-free: loading reports rows through a callback.

// Called for every stored row, ordered by id ascending.
typedef void (*msg_cw_row_cb)(uint32_t id, const char *val);

// Invokes cb for each row. No-op when the database is unavailable.
void msg_cw_store_load(msg_cw_row_cb cb);

// Inserts a new row and returns its rowid (0 on failure).
uint32_t msg_cw_store_new(const char *val);

void msg_cw_store_edit(uint32_t id, const char *val);

void msg_cw_store_delete(uint32_t id);

#ifdef __cplusplus
}
#endif
