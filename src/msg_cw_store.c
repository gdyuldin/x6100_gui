/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "msg_cw_store.h"

#include "cfg/db.h"

#include <string.h>
#include <sqlite3.h>

void msg_cw_store_load(msg_cw_row_cb cb) {
    sqlite3 *db = cfg_db_get();

    if (db == NULL) {
        return;
    }

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, "SELECT id,val FROM msg_cw", -1, &stmt, 0) != SQLITE_OK) {
        return;
    }

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        int                 id  = sqlite3_column_int(stmt, 0);
        const unsigned char *val = sqlite3_column_text(stmt, 1);

        if (cb) {
            cb((uint32_t)id, val ? (const char *)val : "");
        }
    }

    sqlite3_finalize(stmt);
}

uint32_t msg_cw_store_new(const char *val) {
    sqlite3 *db = cfg_db_get();

    if (db == NULL) {
        return 0;
    }

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, "INSERT INTO msg_cw (val) VALUES(?)", -1, &stmt, 0) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, val, strlen(val), 0);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (uint32_t)sqlite3_last_insert_rowid(db);
}

void msg_cw_store_edit(uint32_t id, const char *val) {
    sqlite3 *db = cfg_db_get();

    if (db == NULL) {
        return;
    }

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, "UPDATE msg_cw SET val = ? WHERE id = ?", -1, &stmt, 0) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, val, strlen(val), 0);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void msg_cw_store_delete(uint32_t id) {
    sqlite3 *db = cfg_db_get();

    if (db == NULL) {
        return;
    }

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, "DELETE FROM msg_cw WHERE id = ?", -1, &stmt, 0) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
