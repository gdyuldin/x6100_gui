/**
 * Work with params table on DB
 */
#include "params.private.h"

#include "subjects.private.h"

#include "../lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static sqlite3      *db;
static sqlite3_stmt *write_stmt;
static sqlite3_stmt *read_stmt;


void cfg_params_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM params WHERE name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO params(name, val) VALUES(:name, :val)", -1, &write_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}


int cfg_params_load_item(cfg_item_t *item) {
    int rc;
    rc = sqlite3_bind_text(read_stmt, sqlite3_bind_parameter_index(read_stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        return rc;
    }

    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        switch (item->val->dtype) {
            case DTYPE_INT:
                subject_set_int(item->val, sqlite3_column_int(read_stmt, 0));
                break;
            case DTYPE_UINT64:
                subject_set_uint64(item->val, sqlite3_column_int64(read_stmt, 0));
                break;
            default:
                LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, item->val->dtype);
                return -1;
        }
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = -1;
    }
    sqlite3_reset(read_stmt);
    sqlite3_clear_bindings(read_stmt);
    return rc;
}


int cfg_params_save_item(cfg_item_t *item) {
    int rc;
    sqlite3_bind_text(write_stmt, sqlite3_bind_parameter_index(write_stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    int val_index = sqlite3_bind_parameter_index(write_stmt, ":val");
    switch (item->val->dtype) {
        case DTYPE_INT:
            sqlite3_bind_int(write_stmt, val_index, item->val->int_val);
            break;
        case DTYPE_UINT64:
            sqlite3_bind_int64(write_stmt, val_index, item->val->uint64_val);
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, item->val->dtype);
            sqlite3_reset(write_stmt);
            sqlite3_clear_bindings(write_stmt);
            return -1;
            break;
    }
    rc = sqlite3_step(write_stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
    }
    sqlite3_reset(write_stmt);
    sqlite3_clear_bindings(write_stmt);
    return rc;
}
