#include "atu.private.h"

#include "cfg.h"

#include "../lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STR_INDIR(x) #x
#define STR(x) STR_INDIR(x)

#define ATU_SAVE_STEP 25000

static sqlite3        *db;
static sqlite3_stmt   *insert_stmt;
static sqlite3_stmt   *update_stmt;
static sqlite3_stmt   *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex  = PTHREAD_MUTEX_INITIALIZER;

static void    update_atu_network(Subject *subj, void *user_data);
static void    add_atu_net_to_cache(int32_t freq, uint32_t network);
static void    load_all_atu_for_ant(int32_t ant_id);
static int32_t find_atu_for_freq(int32_t freq);

atu_network_t atu_network;

static struct atu_network_data_st {
    int32_t  freq;
    uint32_t network;
} *atu_network_cache = NULL;

static uint32_t atu_network_cache_size      = 0;
static uint32_t atu_network_cache_allocated = 0;
static uint32_t ant_id                      = -1;

void cfg_atu_init(sqlite3 *database) {
    db = database;
    int rc;

    rc = sqlite3_prepare_v2(db, "SELECT freq, val FROM atu WHERE ant = :ant", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "UPDATE atu SET freq = :freq, val = :val WHERE ant = :ant AND freq = :prev_freq", -1,
                            &update_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare update statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO atu(ant, freq, val) VALUES(:ant, :freq, :val);", -1,
                            &insert_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare insert statement: %s", sqlite3_errmsg(db));
        exit(1);
    }

    atu_network_cache_allocated = 10;
    atu_network_cache           = malloc(sizeof(*atu_network_cache) * atu_network_cache_allocated);

    atu_network.loaded        = subject_create_int(false);
    atu_network.network        = subject_create_int(0);

    subject_add_observer(cfg_cur.fg_freq, update_atu_network, NULL);
    subject_add_observer(cfg.atu_enabled.val, update_atu_network, NULL);
    subject_add_observer_and_call(cfg.ant_id.val, update_atu_network, NULL);
}

int cfg_atu_save_network(uint32_t network) {
    int     rc;
    int32_t ant_id = subject_get_int(cfg.ant_id.val);
    int32_t freq   = subject_get_int(cfg_cur.fg_freq);

    LV_LOG_INFO("Saving ATU network %u for freq: %i and ant: %i\n", network, freq, ant_id);

    sqlite3_stmt *stmt;
    int32_t       atu_pos = find_atu_for_freq(freq);
    pthread_mutex_lock(&write_mutex);
    if (atu_pos >= 0) {
        stmt = update_stmt;
        rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":prev_freq"), atu_network_cache[atu_pos].freq);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind prev_freq %i: %s", atu_network_cache[atu_pos].freq, sqlite3_errmsg(db));
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return rc;
        }
    } else {
        stmt = insert_stmt;
    }

    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":ant"), ant_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind ant_id %i: %s", ant_id, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %i: %s", freq, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":val"), network);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind network %i: %s", network, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save atu_params: %s", sqlite3_errmsg(db));
    } else {
        rc = 0;
        load_all_atu_for_ant(ant_id);
        subject_set_int(atu_network.loaded, true);
        subject_set_int(atu_network.network, network);
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

static void update_atu_network(Subject *subj, void *user_data) {
    if (!subject_get_int(cfg.atu_enabled.val)) {
        return;
    }
    int32_t new_ant_id = subject_get_int(cfg.ant_id.val);
    if (ant_id != new_ant_id) {
        ant_id = new_ant_id;
        load_all_atu_for_ant(ant_id);
    }
    int32_t freq    = subject_get_int(cfg_cur.fg_freq);
    int32_t min_pos = find_atu_for_freq(freq);
    if (min_pos >= 0) {
        subject_set_int(atu_network.loaded, true);
        subject_set_int(atu_network.network, atu_network_cache[min_pos].network);
        LV_LOG_INFO("Loaded ATU network for freq: %i, ant: %i -  %u", freq, ant_id, atu_network_cache[min_pos].network);
    } else {
        subject_set_int(atu_network.loaded, false);
        subject_set_int(atu_network.network, 0);
        LV_LOG_INFO("ATU network for freq: %i, ant: %i not found", freq, ant_id);
    }
}

static void add_atu_net_to_cache(int32_t freq, uint32_t network) {
    if (atu_network_cache_size >= atu_network_cache_allocated) {
        atu_network_cache_allocated += 10;
        atu_network_cache = realloc(atu_network_cache, sizeof(*atu_network_cache) * atu_network_cache_allocated);
    }
    atu_network_cache[atu_network_cache_size].freq    = freq;
    atu_network_cache[atu_network_cache_size].network = network;
    atu_network_cache_size++;
}

static int32_t find_atu_for_freq(int32_t freq) {
    int32_t min_diff = ATU_SAVE_STEP + 1;
    int32_t diff, min_pos;
    // search closest atu network
    for (size_t i = 0; i < atu_network_cache_size; i++) {
        diff = abs(atu_network_cache[i].freq - freq);
        if (diff < min_diff) {
            min_diff = diff;
            min_pos  = i;
        }
    }
    if (min_diff <= ATU_SAVE_STEP) {
        return min_pos;
    } else {
        return -1;
    }
}

static void load_all_atu_for_ant(int32_t ant_id) {
    int           rc;
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":ant"), ant_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind ant_id %i: %s", ant_id, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return;
    }

    atu_network_cache_size = 0;
    while (1) {
        rc = sqlite3_step(stmt);

        if (rc == SQLITE_ROW) {
            add_atu_net_to_cache(sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            LV_LOG_ERROR("Error while reading rows: %s", sqlite3_errmsg(db));
            break;
        }
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);
}
