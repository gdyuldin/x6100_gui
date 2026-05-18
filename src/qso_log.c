/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "qso_log.h"

#include "util.h"
#include "msg.h"
#include "adif.h"

#include <lvgl/src/misc/lv_log.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

static sqlite3_stmt     *search_callsign_stmt=NULL;
static sqlite3_stmt     *search_grid_stmt=NULL;
static sqlite3          *db = NULL;
static pthread_mutex_t   db_mutex = PTHREAD_MUTEX_INITIALIZER;


static bool create_tables();
static void* import_adif_thread(void* args);


bool qso_log_init() {
    int rc = sqlite3_open("/mnt/qso_log.db", &db);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Can't open qso_log.db");
        return false;
    }
    return create_tables();
}

void qso_log_destruct() {
    pthread_mutex_lock(&db_mutex);
    if (search_callsign_stmt) {
        sqlite3_finalize(search_callsign_stmt);
        search_callsign_stmt = NULL;
    }
    if (search_grid_stmt) {
        sqlite3_finalize(search_grid_stmt);
        search_grid_stmt = NULL;
    }
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
    pthread_mutex_unlock(&db_mutex);
}

void qso_log_import_adif(const char * path) {
    pthread_t thr;

    if (access(path, F_OK) != 0) {
        LV_LOG_USER("No ADI file to import");
        return;
    }
    if(pthread_create(&thr, NULL, import_adif_thread, (void*)path) != 0) {
        LV_LOG_ERROR("Import adif thread start failed");
    }
}

qso_log_band_t qso_log_freq_to_band(uint64_t freq_hz)
{
    uint32_t freq_khz = freq_hz  / 1000;

    switch (freq_khz)
    {
    case 1800 ... 2000:
        return BAND_160M;
        break;
    case 3500 ... 4000:
        return BAND_80M;
        break;
    case 5351 ... 5367:
        return BAND_60M;
        break;
    case 7000 ... 7300:
        return BAND_40M;
        break;
    case 10100 ... 10150:
        return BAND_30M;
        break;
    case 14000 ... 14350:
        return BAND_20M;
        break;
    case 18068 ... 18168:
        return BAND_17M;
        break;
    case 21000 ... 21450:
        return BAND_15M;
        break;
    case 24890 ... 24990:
        return BAND_12M;
        break;
    case 28000 ... 29700:
        return BAND_10M;
        break;
    case 50000 ... 54000:
        return BAND_6M;
        break;
    default:
        return BAND_OTHER;
        break;
    }
}

qso_log_record_t qso_log_record_create(const char * local_call, const char * remote_call,
    time_t qso_time, qso_log_mode_t mode, int rsts, int rstr, uint64_t freq_hz,
    const char * name, const char * qth, const char *local_grid, const char * remote_grid)
{
    qso_log_record_t rec = {
        .time = qso_time,
        .mode = mode,
        .rsts = rsts,
        .rstr = rstr,
        .freq_mhz = (float) freq_hz / 1000000,
        .band = qso_log_freq_to_band(freq_hz)
    };
    strncpy(rec.local_call, local_call, sizeof(rec.local_call) - 1);
    rec.local_call[sizeof(rec.local_call) - 1] = 0;

    strncpy(rec.remote_call, remote_call, sizeof(rec.remote_call) - 1);
    rec.remote_call[sizeof(rec.remote_call) - 1] = 0;

    if (name) {
        strncpy(rec.name, name, sizeof(rec.name) - 1);
        rec.name[sizeof(rec.name) - 1] = 0;
    }

    if (qth) {
        strncpy(rec.qth, qth, sizeof(rec.qth) - 1);
        rec.qth[sizeof(rec.qth) - 1] = 0;
    }

    if (local_grid) {
        strncpy(rec.local_grid, local_grid, sizeof(rec.local_grid) - 1);
        rec.local_grid[sizeof(rec.local_grid) - 1] = 0;
    }

    if (remote_grid) {
        strncpy(rec.remote_grid, remote_grid, sizeof(rec.remote_grid) - 1);
        rec.remote_grid[sizeof(rec.remote_grid) - 1] = 0;
    }
    return rec;
}


static inline int bind_optional_text(sqlite3_stmt * stmt, int pos, const char * val) {
    if (!val) {
        return sqlite3_bind_null(stmt, pos);
    } else {
        return sqlite3_bind_text(stmt, pos, val, strlen(val), 0);
    }
}

int qso_log_record_save(qso_log_record_t qso) {
    sqlite3_stmt    *stmt;
    int             rc;

    if (strlen(qso.local_call) == 0) {
        LV_LOG_ERROR("Local callsign is required");
        return -1;
    }
    if (strlen(qso.remote_call) == 0) {
        LV_LOG_ERROR("Remote callsign is required");
        return -1;
    }

    rc = sqlite3_prepare_v2(
        db, "INSERT OR IGNORE INTO qso_log ("
                "ts, freq, band, mode, local_callsign, remote_callsign, rsts, rstr, "
                "local_grid, remote_grid, op_name, canonized_remote_callsign"
            ") VALUES (datetime(:ts, 'unixepoch'), :freq, :band, :mode, :local_callsign, :remote_callsign, "
                ":rsts, :rstr, :local_grid, :remote_grid, :op_name, :canonized_remote_callsign)",
                       -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Error in prepairing query");
        return -1;
    }
    rc = sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, ":ts"), qso.time);
    if (rc != SQLITE_OK) {
        printf("check point: %i\n", rc);
        printf("column_id: %i\n", sqlite3_bind_parameter_index(stmt, ":ts"));
        fflush(stdout);
        return -1;
    }
    rc = sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), (double) qso.freq_mhz);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":band"), qso.band);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":mode"), qso.mode);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":local_callsign"), qso.local_call, strlen(qso.local_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":remote_callsign"), qso.remote_call, strlen(qso.remote_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":rsts"), qso.rsts);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":rstr"), qso.rstr);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":local_grid"), qso.local_grid);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":remote_grid"), qso.remote_grid);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":op_name"), qso.name);
    if (rc != SQLITE_OK) return -1;

    char * canonized_remote_callsign = util_canonize_callsign(qso.remote_call, true);

    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":canonized_remote_callsign"), canonized_remote_callsign);
    if (rc != SQLITE_OK) {
        free(canonized_remote_callsign);
        return -1;
    }

    if(sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Error during execute: `%s`\n", sqlite3_expanded_sql(stmt));
        free(canonized_remote_callsign);
        return -1;
    }

    int changed = sqlite3_changes(db);
    if (changed == 0) {
        printf("Not inserted `%s`\n", sqlite3_expanded_sql(stmt));
    }

    free(canonized_remote_callsign);

    sqlite3_finalize(stmt);
    return changed;
}


qso_log_search_worked_t qso_log_search_worked(const char *callsign, qso_log_mode_t mode, qso_log_band_t band)
{
    int                         rc;
    qso_log_search_worked_t     worked = SEARCH_WORKED_NO;

    if (!search_callsign_stmt) {
        rc = sqlite3_prepare_v3(db, "SELECT DISTINCT band, mode FROM qso_log WHERE canonized_remote_callsign LIKE ?",
                        -1, SQLITE_PREPARE_PERSISTENT, &search_callsign_stmt, 0);
        if (rc != SQLITE_OK) {
            return -1;
        }
    } else {
        sqlite3_reset(search_callsign_stmt);
        sqlite3_clear_bindings(search_callsign_stmt);
    }

    char * canonized_callsign = util_canonize_callsign(callsign, true);
    if (!canonized_callsign) {
        canonized_callsign = strdup(callsign);
    }
    rc = sqlite3_bind_text(search_callsign_stmt, 1, canonized_callsign, strlen(canonized_callsign), 0);
    if (rc != SQLITE_OK) {
        free(canonized_callsign);
        return -1;
    }

    while (sqlite3_step(search_callsign_stmt) != SQLITE_DONE) {
        worked = SEARCH_WORKED_YES;
        if ((sqlite3_column_int(search_callsign_stmt, 0) == band) &&
            (sqlite3_column_int(search_callsign_stmt, 1) == mode))
        {
            worked = SEARCH_WORKED_SAME_MODE;
            break;
        }
    }

    free(canonized_callsign);
    return worked;
}


static void * import_adif_thread(void* args) {

    char *path = (char* )args;

    pthread_detach(pthread_self());

    qso_log_record_t * records;
    int cnt = adif_read(path, &records);
    char * canonized;
    size_t updated_rows = 0;
    size_t c = 0;
    for (size_t i = 0; i < cnt; i++) {
        int changed = qso_log_record_save(records[i]);
        c++;
        if (changed > 0) {
            updated_rows += changed;
        }
        if (c >= 10) {
            c = 0;
            msg_update_text_fmt("Importing QSO: %zu/%zu", i + 1, cnt);
        }
    }
    free(records);

    char new_path[128] = {0};
    snprintf(new_path, sizeof(new_path), "%s.bak", path);
    rename(path, new_path);
    msg_update_text_fmt("Imported %u QSOs from %i", updated_rows, cnt);
    pthread_exit(NULL);
}



static bool create_tables() {
    char    *err = 0;
    int     rc;

    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS qso_log( "
            "ts              TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
            "freq            REAL CHECK ( freq > 0 ), "
            "band            INT NOT NULL, "
            "mode            INT NOT NULL, "
            "local_callsign  TEXT NOT NULL, "
            "remote_callsign TEXT NOT NULL, "
            "canonized_remote_callsign TEXT NOT NULL, "
            "rsts            INTEGER NOT NULL, "
            "rstr            INTEGER NOT NULL, "
            "local_qth       TEXT, "
            "remote_qth      TEXT, "
            "local_grid      TEXT, "
            "remote_grid     TEXT, "
            "op_name         TEXT, "
            "comment         TEXT "
        ")",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_canonized_remote_callsign ON qso_log(canonized_remote_callsign COLLATE NOCASE)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }
    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_mode ON qso_log(mode)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }
    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_ts ON qso_log(ts)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    rc = sqlite3_exec(db,
        "CREATE UNIQUE INDEX IF NOT EXISTS qso_log_idx_ts_call ON qso_log(ts, remote_callsign)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    return true;
}

bool qso_log_search_worked_grid(const char *grid, qso_log_mode_t mode, qso_log_band_t band)
{
    if (!db || !grid || strlen(grid) == 0) return false;

    pthread_mutex_lock(&db_mutex);

    int rc;
    if (!search_grid_stmt) {
        rc = sqlite3_prepare_v3(
            db,
            "SELECT 1 FROM qso_log WHERE remote_grid LIKE ? AND band = ? AND mode = ? LIMIT 1",
            -1,
            SQLITE_PREPARE_PERSISTENT,
            &search_grid_stmt,
            NULL);
        if (rc != SQLITE_OK) {
            pthread_mutex_unlock(&db_mutex);
            return false;
        }
    } else {
        sqlite3_reset(search_grid_stmt);
        sqlite3_clear_bindings(search_grid_stmt);
    }

    /* Use prefix match so grid "FN31" matches "FN31" or "FN31xx" */
    char pattern[32] = {0};
    snprintf(pattern, sizeof(pattern), "%s%%", grid);

    rc = sqlite3_bind_text(search_grid_stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return false; }
    rc = sqlite3_bind_int(search_grid_stmt, 2, band);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return false; }
    rc = sqlite3_bind_int(search_grid_stmt, 3, mode);
    if (rc != SQLITE_OK) { pthread_mutex_unlock(&db_mutex); return false; }

    bool found = (sqlite3_step(search_grid_stmt) == SQLITE_ROW);
    pthread_mutex_unlock(&db_mutex);
    return found;
}
