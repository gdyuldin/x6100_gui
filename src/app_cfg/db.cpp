/**
 * Work with params table on DB (new app_cfg version).
 * The sqlite3 connection and prepared statements are shared with the
 * template-based cfg_param_load/cfg_param_save defined in db.h.
 *
 * Each statement group lives as inline-static fields of its table class
 * (stmt + guarding mutex + cached :name/:id/:val parameter indices) with a
 * common prefix per group. Indices are resolved once at Init via
 * sqlite3_bind_parameter_index() and reused on every bind, which avoids
 * per-call name lookups.
 */
#include "db.h"

#include <stdlib.h>
#include <pthread.h>
#include "../lvgl/lvgl.h"


// ---------------------------------------------------------------------------
// ParamsTable
// ---------------------------------------------------------------------------

bool ParamsTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated ParamsTable initialization");
        return false;
    }
    db_ = database;

    int rc;
    rc = sqlite3_prepare_v2(db_, "SELECT val FROM params WHERE name = :name", -1, &load_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    load_name_param_index_ = sqlite3_bind_parameter_index(load_stmt_, ":name");

    rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO params(name, val) VALUES(:name, :val)", -1, &save_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(load_stmt_);
        load_stmt_           = nullptr;
        load_name_param_index_ = 0;
        db_ = nullptr;
        return false;
    }
    save_name_param_index_ = sqlite3_bind_parameter_index(save_stmt_, ":name");
    save_val_param_index_  = sqlite3_bind_parameter_index(save_stmt_, ":val");
    return true;
}

void ParamsTable::Shutdown() {
    if (load_stmt_) {
        sqlite3_finalize(load_stmt_);
        load_stmt_ = nullptr;
    }
    if (save_stmt_) {
        sqlite3_finalize(save_stmt_);
        save_stmt_ = nullptr;
    }
    load_name_param_index_ = 0;
    save_name_param_index_ = 0;
    save_val_param_index_  = 0;
    db_ = nullptr;
}


// ---------------------------------------------------------------------------
// BandsTable
// ---------------------------------------------------------------------------

bool BandsTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated BandsTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(db_, "SELECT name, start_freq, stop_freq, type FROM bands WHERE id = :id", -1,
                            &get_band_by_id_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get_band_by_id statement: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    get_band_by_id_id_param_index_ = sqlite3_bind_parameter_index(get_band_by_id_stmt_, ":id");

    rc = sqlite3_prepare_v2(
        db_,
        "SELECT id, name, start_freq, stop_freq FROM bands WHERE "
        "   (:freq >= start_freq) AND (:freq <= stop_freq) AND (type = 1) "
        "UNION SELECT * FROM ("
        "   SELECT NULL, NULL, a.stop_freq, b.start_freq FROM ("
        "       SELECT stop_freq FROM bands WHERE :freq > stop_freq AND type = 1 ORDER BY stop_freq DESC LIMIT 1"
        "   ) AS a FULL OUTER JOIN ("
        "       SELECT start_freq FROM bands WHERE :freq < start_freq AND type = 1 ORDER BY start_freq LIMIT 1"
        "   ) AS b"
        ") ORDER BY id DESC NULLS LAST LIMIT 1",
        -1, &get_band_by_freq_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get_band_by_freq statement: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_band_by_id_stmt_);
        get_band_by_id_stmt_ = nullptr;
        db_ = nullptr;
        return false;
    }
    get_band_by_freq_freq_param_index_ = sqlite3_bind_parameter_index(get_band_by_freq_stmt_, ":freq");

    rc = sqlite3_prepare_v2(db_,
                            "SELECT id, name, start_freq, stop_freq, type FROM bands "
                            "WHERE :freq <= start_freq AND id != :id AND type = 1 ORDER BY start_freq LIMIT 1",
                            -1, &get_band_up_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get_band_up statement: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_band_by_id_stmt_);
        sqlite3_finalize(get_band_by_freq_stmt_);
        get_band_by_id_stmt_   = nullptr;
        get_band_by_freq_stmt_ = nullptr;
        db_ = nullptr;
        return false;
    }
    get_band_up_freq_param_index_ = sqlite3_bind_parameter_index(get_band_up_stmt_, ":freq");
    get_band_up_id_param_index_   = sqlite3_bind_parameter_index(get_band_up_stmt_, ":id");

    rc = sqlite3_prepare_v2(db_,
                            "SELECT id, name, start_freq, stop_freq, type FROM bands "
                            "WHERE :freq >= stop_freq AND id != :id AND type = 1 ORDER BY start_freq DESC LIMIT 1",
                            -1, &get_band_down_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get_band_down statement: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_band_by_id_stmt_);
        sqlite3_finalize(get_band_by_freq_stmt_);
        sqlite3_finalize(get_band_up_stmt_);
        get_band_by_id_stmt_   = nullptr;
        get_band_by_freq_stmt_ = nullptr;
        get_band_up_stmt_      = nullptr;
        db_ = nullptr;
        return false;
    }
    get_band_down_freq_param_index_ = sqlite3_bind_parameter_index(get_band_down_stmt_, ":freq");
    get_band_down_id_param_index_   = sqlite3_bind_parameter_index(get_band_down_stmt_, ":id");

    rc = sqlite3_prepare_v2(db_, "SELECT id, name, start_freq, stop_freq, type FROM bands ORDER BY start_freq", -1,
                            &read_all_bands_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read_all_bands statement: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_band_by_id_stmt_);
        sqlite3_finalize(get_band_by_freq_stmt_);
        sqlite3_finalize(get_band_up_stmt_);
        sqlite3_finalize(get_band_down_stmt_);
        get_band_by_id_stmt_   = nullptr;
        get_band_by_freq_stmt_ = nullptr;
        get_band_up_stmt_      = nullptr;
        get_band_down_stmt_    = nullptr;
        db_ = nullptr;
        return false;
    }
    return true;
}

void BandsTable::Shutdown() {
    if (get_band_by_id_stmt_) {
        sqlite3_finalize(get_band_by_id_stmt_);
        get_band_by_id_stmt_ = nullptr;
    }
    if (get_band_by_freq_stmt_) {
        sqlite3_finalize(get_band_by_freq_stmt_);
        get_band_by_freq_stmt_ = nullptr;
    }
    if (get_band_up_stmt_) {
        sqlite3_finalize(get_band_up_stmt_);
        get_band_up_stmt_ = nullptr;
    }
    if (get_band_down_stmt_) {
        sqlite3_finalize(get_band_down_stmt_);
        get_band_down_stmt_ = nullptr;
    }
    if (read_all_bands_stmt_) {
        sqlite3_finalize(read_all_bands_stmt_);
        read_all_bands_stmt_ = nullptr;
    }
    get_band_by_id_id_param_index_       = 0;
    get_band_by_freq_freq_param_index_   = 0;
    get_band_up_freq_param_index_        = 0;
    get_band_up_id_param_index_          = 0;
    get_band_down_freq_param_index_      = 0;
    get_band_down_id_param_index_        = 0;
    {
        std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
        last_band = BandInfo{};
    }
    db_ = nullptr;
}

BandInfoLoadResult BandsTable::get_by_id(int32_t band_id) {
    if (band_id == BAND_UNDEFINED) {
        return {BandInfo{}, NOT_FOUND};
    }

    // Fast path: serve from cache without touching the DB.
    {
        std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
        if (last_band.id == band_id) {
            return {last_band, SUCCESS};
        }
    }

    LV_LOG_USER("Loading band info for id: %i", band_id);

    int            rc;
    StmtResetGuard guard(get_band_by_id_mutex_, get_band_by_id_stmt_);

    rc = sqlite3_bind_int(get_band_by_id_stmt_, get_band_by_id_id_param_index_, band_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db_));
        return {BandInfo{}, rc};
    }
    rc = sqlite3_step(get_band_by_id_stmt_);
    if (rc == SQLITE_ROW) {
        BandInfo info;
        info.id              = band_id;
        const unsigned char *txt = sqlite3_column_text(get_band_by_id_stmt_, 0);
        info.name            = txt ? reinterpret_cast<const char *>(txt) : "";
        info.start_freq      = sqlite3_column_int(get_band_by_id_stmt_, 1);
        info.stop_freq       = sqlite3_column_int(get_band_by_id_stmt_, 2);
        info.type            = static_cast<band_type_t>(sqlite3_column_int(get_band_by_id_stmt_, 3));
        {
            std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
            last_band = info;
        }
        return {info, SUCCESS};
    }
    LV_LOG_USER("No info for band with id: %i", band_id);
    return {BandInfo{}, NOT_FOUND};
}

BandInfoLoadResult BandsTable::get_by_freq(uint32_t freq) {
    {
        std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
        if ((last_band.id != BAND_UNDEFINED) && (freq >= last_band.start_freq) && (freq <= last_band.stop_freq)) {
            return {last_band, SUCCESS};
        }
    }

    LV_LOG_USER("Loading band info for freq: %u", freq);
    int            rc;
    StmtResetGuard guard(get_band_by_freq_mutex_, get_band_by_freq_stmt_);
    rc = sqlite3_bind_int(get_band_by_freq_stmt_, get_band_by_freq_freq_param_index_, freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %u: %s", freq, sqlite3_errmsg(db_));
        return {BandInfo{}, rc};
    }
    rc = sqlite3_step(get_band_by_freq_stmt_);
    if (rc == SQLITE_ROW) {
        BandInfo info;
        if (sqlite3_column_type(get_band_by_freq_stmt_, 0) != SQLITE_NULL) {
            // Found an active band
            info.id                  = sqlite3_column_int(get_band_by_freq_stmt_, 0);
            const unsigned char *txt = sqlite3_column_text(get_band_by_freq_stmt_, 1);
            info.name                = txt ? reinterpret_cast<const char *>(txt) : "";
            info.type                = BAND_ACTIVE;
        } else {
            // Found a gap between bands
            info.id   = BAND_UNDEFINED;
            info.type = BAND_INACTIVE;
        }
        info.start_freq = sqlite3_column_int(get_band_by_freq_stmt_, 2);
        if (sqlite3_column_type(get_band_by_freq_stmt_, 3) == SQLITE_NULL) {
            info.stop_freq = 0xFFFFFFFFU;
        } else {
            info.stop_freq = sqlite3_column_int(get_band_by_freq_stmt_, 3);
        }
        {
            std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
            last_band = info;
        }
        return {info, SUCCESS};
    }
    LV_LOG_WARN("No band info for freq: %u", freq);
    return {BandInfo{}, NOT_FOUND};
}

BandInfoLoadResult BandsTable::next(int32_t cur_band_id, uint32_t cur_freq, bool up) {
    int           rc;
    sqlite3_stmt *stmt;
    std::mutex* mux;
    int freq_idx, band_idx;
    if (up) {
        stmt = get_band_up_stmt_;
        mux = &get_band_up_mutex_;
        freq_idx = get_band_up_freq_param_index_;
        band_idx = get_band_up_id_param_index_;
    } else {
        stmt = get_band_down_stmt_;
        mux = &get_band_down_mutex_;
        freq_idx = get_band_down_freq_param_index_;
        band_idx = get_band_down_id_param_index_;
    }

    StmtResetGuard guard(*mux, stmt);

    rc = sqlite3_bind_int(stmt, freq_idx, cur_freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %u to find up/down stmt: %s", cur_freq, sqlite3_errmsg(db_));
        return {BandInfo{}, rc};
    }
    rc = sqlite3_bind_int(stmt, band_idx, cur_band_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to current band id %i to find up/down stmt: %s", cur_band_id, sqlite3_errmsg(db_));
        return {BandInfo{}, rc};
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        BandInfo info;
        info.id         = sqlite3_column_int(stmt, 0);
        const unsigned char *txt = sqlite3_column_text(stmt, 1);
        info.name            = txt ? reinterpret_cast<const char *>(txt) : "";
        info.type     = BAND_ACTIVE;
        info.start_freq = sqlite3_column_int(stmt, 2);
        info.stop_freq  = sqlite3_column_int(stmt, 3);
        {
            std::lock_guard<std::mutex> cache_lock(last_band_mutex_);
            last_band = info;
        }
        return {info, SUCCESS};
    }
    LV_LOG_INFO("No next band info for freq: %u, cur_id: %i and direction: %i", cur_freq, cur_band_id, up);
    return {BandInfo{}, NOT_FOUND};
}

std::vector<BandInfo> BandsTable::all_bands() {
    int           rc;
    StmtResetGuard guard(read_all_bands_mutex_, read_all_bands_stmt_);
    std::vector<BandInfo> result;
    while (1) {
        rc = sqlite3_step(read_all_bands_stmt_);

        if (rc == SQLITE_ROW) {
            BandInfo info;
            info.id         = sqlite3_column_int(read_all_bands_stmt_, 0);
            const unsigned char *txt = sqlite3_column_text(read_all_bands_stmt_, 1);
            info.name            = txt ? reinterpret_cast<const char *>(txt) : "";
            info.start_freq = sqlite3_column_int(read_all_bands_stmt_, 2);
            info.stop_freq  = sqlite3_column_int(read_all_bands_stmt_, 3);
            info.type     = static_cast<band_type_t>(sqlite3_column_int(read_all_bands_stmt_, 4));
            result.push_back(info);
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            LV_LOG_ERROR("Error while reading bands rows: %s", sqlite3_errmsg(db_));
            break;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// BandParamsTable
// ---------------------------------------------------------------------------

bool BandParamsTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated BandParamsTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(db_, "SELECT val FROM band_params WHERE bands_id = :id AND name = :name", -1, &load_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare BandParamsTable::load: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    load_id_param_index_   = sqlite3_bind_parameter_index(load_stmt_, ":id");
    load_name_param_index_ = sqlite3_bind_parameter_index(load_stmt_, ":name");

    rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO band_params(bands_id, name, val) VALUES(:id, :name, :val)", -1,
                            &save_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare BandParamsTable::save: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(load_stmt_);
        load_stmt_             = nullptr;
        load_id_param_index_   = 0;
        load_name_param_index_ = 0;
        db_ = nullptr;
        return false;
    }
    save_id_param_index_   = sqlite3_bind_parameter_index(save_stmt_, ":id");
    save_name_param_index_ = sqlite3_bind_parameter_index(save_stmt_, ":name");
    save_val_param_index_  = sqlite3_bind_parameter_index(save_stmt_, ":val");
    return true;
}

void BandParamsTable::Shutdown() {
    if (load_stmt_) {
        sqlite3_finalize(load_stmt_);
        load_stmt_ = nullptr;
    }
    if (save_stmt_) {
        sqlite3_finalize(save_stmt_);
        save_stmt_ = nullptr;
    }
    load_id_param_index_    = 0;
    load_name_param_index_  = 0;
    save_id_param_index_    = 0;
    save_name_param_index_  = 0;
    save_val_param_index_   = 0;
    db_ = nullptr;
}


// ---------------------------------------------------------------------------
// ModeParamsTable
// ---------------------------------------------------------------------------

bool ModeParamsTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated ModeParamsTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(db_, "SELECT val FROM mode_params WHERE mode = :id AND name = :name", -1, &load_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare ModeParamsTable::load: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    load_id_param_index_   = sqlite3_bind_parameter_index(load_stmt_, ":id");
    load_name_param_index_ = sqlite3_bind_parameter_index(load_stmt_, ":name");

    rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO mode_params(mode, name, val) VALUES(:id, :name, :val)", -1,
                            &save_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare ModeParamsTable::save: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(load_stmt_);
        load_stmt_             = nullptr;
        load_id_param_index_   = 0;
        load_name_param_index_ = 0;
        db_ = nullptr;
        return false;
    }
    save_id_param_index_   = sqlite3_bind_parameter_index(save_stmt_, ":id");
    save_name_param_index_ = sqlite3_bind_parameter_index(save_stmt_, ":name");
    save_val_param_index_  = sqlite3_bind_parameter_index(save_stmt_, ":val");
    return true;
}

void ModeParamsTable::Shutdown() {
    if (load_stmt_) {
        sqlite3_finalize(load_stmt_);
        load_stmt_ = nullptr;
    }
    if (save_stmt_) {
        sqlite3_finalize(save_stmt_);
        save_stmt_ = nullptr;
    }
    load_id_param_index_    = 0;
    load_name_param_index_  = 0;
    save_id_param_index_    = 0;
    save_name_param_index_  = 0;
    save_val_param_index_   = 0;
    db_ = nullptr;
}

// ---------------------------------------------------------------------------
// TransverterTable
// ---------------------------------------------------------------------------

bool TransverterTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated TransverterTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(db_, "SELECT val FROM transverter WHERE name = :name AND id = :id", -1, &load_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare TransverterTable::load: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    load_name_param_index_ = sqlite3_bind_parameter_index(load_stmt_, ":name");
    load_id_param_index_   = sqlite3_bind_parameter_index(load_stmt_, ":id");

    rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO transverter(id, name, val) VALUES(:id, :name, :val)", -1,
                            &save_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare TransverterTable::save: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(load_stmt_);
        load_stmt_             = nullptr;
        load_name_param_index_ = 0;
        load_id_param_index_   = 0;
        db_ = nullptr;
        return false;
    }
    save_id_param_index_   = sqlite3_bind_parameter_index(save_stmt_, ":id");
    save_name_param_index_ = sqlite3_bind_parameter_index(save_stmt_, ":name");
    save_val_param_index_  = sqlite3_bind_parameter_index(save_stmt_, ":val");
    return true;
}

void TransverterTable::Shutdown() {
    if (load_stmt_) {
        sqlite3_finalize(load_stmt_);
        load_stmt_ = nullptr;
    }
    if (save_stmt_) {
        sqlite3_finalize(save_stmt_);
        save_stmt_ = nullptr;
    }
    load_id_param_index_    = 0;
    load_name_param_index_  = 0;
    save_id_param_index_    = 0;
    save_name_param_index_  = 0;
    save_val_param_index_   = 0;
    db_ = nullptr;
}


// ---------------------------------------------------------------------------
// MemoryTable
// ---------------------------------------------------------------------------

bool MemoryTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated MemoryTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(db_, "SELECT name, val FROM memory WHERE id = :id", -1, &load_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare MemoryTable::load: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    load_id_param_index_ = sqlite3_bind_parameter_index(load_stmt_, ":id");

    rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO memory(id, name, val) VALUES(:id, :name, :val)", -1,
                            &save_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare MemoryTable::save: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(load_stmt_);
        load_stmt_           = nullptr;
        load_id_param_index_ = 0;
        db_                  = nullptr;
        return false;
    }
    save_id_param_index_   = sqlite3_bind_parameter_index(save_stmt_, ":id");
    save_name_param_index_ = sqlite3_bind_parameter_index(save_stmt_, ":name");
    save_val_param_index_  = sqlite3_bind_parameter_index(save_stmt_, ":val");
    return true;
}

void MemoryTable::Shutdown() {
    if (load_stmt_) {
        sqlite3_finalize(load_stmt_);
        load_stmt_ = nullptr;
    }
    if (save_stmt_) {
        sqlite3_finalize(save_stmt_);
        save_stmt_ = nullptr;
    }
    load_id_param_index_   = 0;
    save_id_param_index_   = 0;
    save_name_param_index_ = 0;
    save_val_param_index_  = 0;
    db_                    = nullptr;
}

int MemoryTable::Save(int32_t id, const char *name, int32_t value) {
    int            rc;
    StmtResetGuard guard(save_mutex_, save_stmt_);

    rc = sqlite3_bind_int(save_stmt_, save_id_param_index_, id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mem id %i: %s", id, sqlite3_errmsg(db_));
        return rc;
    }
    rc = sqlite3_bind_text(save_stmt_, save_name_param_index_, name, strlen(name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mem name %s: %s", name, sqlite3_errmsg(db_));
        return rc;
    }
    rc = sqlite3_bind_int(save_stmt_, save_val_param_index_, value);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mem val %i: %s", value, sqlite3_errmsg(db_));
        return rc;
    }
    rc = sqlite3_step(save_stmt_);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save memory item %s: %s", name, sqlite3_errmsg(db_));
        return rc;
    }
    return SUCCESS;
}

bool MemoryTable::Load(int32_t id,
                       int32_t &freq, bool &has_freq,
                       int32_t &mode, bool &has_mode,
                       int32_t &agc,  bool &has_agc,
                       int32_t &att,  bool &has_att,
                       int32_t &pre,  bool &has_pre) {
    freq     = 0;
    mode     = 0;
    agc      = 0;
    att      = 0;
    pre      = 0;
    has_freq = false;
    has_mode = false;
    has_agc  = false;
    has_att  = false;
    has_pre  = false;

    int            rc;
    StmtResetGuard guard(load_mutex_, load_stmt_);

    rc = sqlite3_bind_int(load_stmt_, load_id_param_index_, id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mem id %i: %s", id, sqlite3_errmsg(db_));
        return false;
    }
    while (1) {
        rc = sqlite3_step(load_stmt_);
        if (rc == SQLITE_ROW) {
            const unsigned char *name_txt = sqlite3_column_text(load_stmt_, 0);
            const char          *name     = name_txt ? reinterpret_cast<const char *>(name_txt) : "";
            const int32_t        val      = sqlite3_column_int(load_stmt_, 1);
            if (strcmp(name, "vfoa_freq") == 0) {
                freq     = val;
                has_freq = true;
            } else if (strcmp(name, "vfoa_mode") == 0) {
                mode     = val;
                has_mode = true;
            } else if (strcmp(name, "vfoa_agc") == 0) {
                agc     = val;
                has_agc = true;
            } else if (strcmp(name, "vfoa_att") == 0) {
                att     = val;
                has_att = true;
            } else if (strcmp(name, "vfoa_pre") == 0) {
                pre     = val;
                has_pre = true;
            }
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            LV_LOG_ERROR("Error while reading memory rows: %s", sqlite3_errmsg(db_));
            return false;
        }
    }
    return has_freq;
}

// ---------------------------------------------------------------------------
// DigitalModesTable
// ---------------------------------------------------------------------------

bool DigitalModesTable::Init(sqlite3 *database) {
    if (db_) {
        LV_LOG_ERROR("Repeated DigitalModesTable initialization");
        return false;
    }
    db_ = database;

    int rc;

    rc = sqlite3_prepare_v2(
        db_, "SELECT label, freq, mode FROM digital_modes WHERE type = :type AND freq > :freq ORDER BY freq ASC LIMIT 1",
        -1, &get_next_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare DigitalModesTable::get_next: %s", sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }
    get_next_type_param_index_ = sqlite3_bind_parameter_index(get_next_stmt_, ":type");
    get_next_freq_param_index_ = sqlite3_bind_parameter_index(get_next_stmt_, ":freq");

    rc = sqlite3_prepare_v2(
        db_, "SELECT label, freq, mode FROM digital_modes WHERE type = :type ORDER BY ABS(freq - :freq) ASC LIMIT 1",
        -1, &get_closest_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare DigitalModesTable::get_closest: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_next_stmt_);
        get_next_stmt_             = nullptr;
        get_next_type_param_index_ = 0;
        get_next_freq_param_index_ = 0;
        db_ = nullptr;
        return false;
    }
    get_closest_type_param_index_ = sqlite3_bind_parameter_index(get_closest_stmt_, ":type");
    get_closest_freq_param_index_ = sqlite3_bind_parameter_index(get_closest_stmt_, ":freq");

    rc = sqlite3_prepare_v2(
        db_, "SELECT label, freq, mode FROM digital_modes WHERE type = :type AND freq < :freq ORDER BY freq DESC LIMIT 1",
        -1, &get_prev_stmt_, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare DigitalModesTable::get_prev: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(get_next_stmt_);
        sqlite3_finalize(get_closest_stmt_);
        get_next_stmt_               = nullptr;
        get_closest_stmt_            = nullptr;
        get_next_type_param_index_   = 0;
        get_next_freq_param_index_   = 0;
        get_closest_type_param_index_ = 0;
        get_closest_freq_param_index_ = 0;
        db_ = nullptr;
        return false;
    }
    get_prev_type_param_index_ = sqlite3_bind_parameter_index(get_prev_stmt_, ":type");
    get_prev_freq_param_index_ = sqlite3_bind_parameter_index(get_prev_stmt_, ":freq");
    return true;
}

void DigitalModesTable::Shutdown() {
    if (get_next_stmt_) {
        sqlite3_finalize(get_next_stmt_);
        get_next_stmt_ = nullptr;
    }
    if (get_closest_stmt_) {
        sqlite3_finalize(get_closest_stmt_);
        get_closest_stmt_ = nullptr;
    }
    if (get_prev_stmt_) {
        sqlite3_finalize(get_prev_stmt_);
        get_prev_stmt_ = nullptr;
    }
    get_next_type_param_index_         = 0;
    get_next_freq_param_index_         = 0;
    get_closest_type_param_index_      = 0;
    get_closest_freq_param_index_      = 0;
    get_prev_type_param_index_         = 0;
    get_prev_freq_param_index_         = 0;
    db_ = nullptr;
}

DigitalModesTable::LoadResult DigitalModesTable::find_next(int32_t type, int32_t current_freq) {
    int            rc;
    StmtResetGuard guard(get_next_mutex_, get_next_stmt_);

    rc = sqlite3_bind_int(get_next_stmt_, get_next_type_param_index_, type);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital type %i to find_next stmt: %s", type, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_bind_int(get_next_stmt_, get_next_freq_param_index_, current_freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital freq %i to find_next stmt: %s", current_freq, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_step(get_next_stmt_);
    if (rc == SQLITE_ROW) {
        Record              record;
        const unsigned char *txt = sqlite3_column_text(get_next_stmt_, 0);
        record.label = txt ? reinterpret_cast<const char *>(txt) : "";
        record.freq  = sqlite3_column_int(get_next_stmt_, 1);
        record.mode  = sqlite3_column_int(get_next_stmt_, 2);
        // record.label is copied into a std::string: sqlite3_reset (run by the
        // guard on scope exit) invalidates the column text pointer.
        return {record, SUCCESS};
    }
    if (rc == SQLITE_DONE) {
        LV_LOG_WARN("No next digital mode for type=%i, freq=%i", type, current_freq);
        return {Record{}, NOT_FOUND};
    }
    LV_LOG_WARN("find_next failed: %s", sqlite3_errmsg(db_));
    return {Record{}, rc};
}

DigitalModesTable::LoadResult DigitalModesTable::find_closest(int32_t type, int32_t current_freq) {
    int            rc;
    StmtResetGuard guard(get_closest_mutex_, get_closest_stmt_);

    rc = sqlite3_bind_int(get_closest_stmt_, get_closest_type_param_index_, type);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital type %i to find_closest stmt: %s", type, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_bind_int(get_closest_stmt_, get_closest_freq_param_index_, current_freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital freq %i to find_closest stmt: %s", current_freq, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_step(get_closest_stmt_);
    if (rc == SQLITE_ROW) {
        Record              record;
        const unsigned char *txt = sqlite3_column_text(get_closest_stmt_, 0);
        record.label = txt ? reinterpret_cast<const char *>(txt) : "";
        record.freq  = sqlite3_column_int(get_closest_stmt_, 1);
        record.mode  = sqlite3_column_int(get_closest_stmt_, 2);
        return {record, SUCCESS};
    }
    if (rc == SQLITE_DONE) {
        LV_LOG_WARN("No closest digital mode for type=%i, freq=%i", type, current_freq);
        return {Record{}, NOT_FOUND};
    }
    LV_LOG_WARN("find_closest failed: %s", sqlite3_errmsg(db_));
    return {Record{}, rc};
}

DigitalModesTable::LoadResult DigitalModesTable::find_prev(int32_t type, int32_t current_freq) {
    int            rc;
    StmtResetGuard guard(get_prev_mutex_, get_prev_stmt_);

    rc = sqlite3_bind_int(get_prev_stmt_, get_prev_type_param_index_, type);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital type %i to find_prev stmt: %s", type, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_bind_int(get_prev_stmt_, get_prev_freq_param_index_, current_freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind digital freq %i to find_prev stmt: %s", current_freq, sqlite3_errmsg(db_));
        return {Record{}, rc};
    }
    rc = sqlite3_step(get_prev_stmt_);
    if (rc == SQLITE_ROW) {
        Record              record;
        const unsigned char *txt = sqlite3_column_text(get_prev_stmt_, 0);
        record.label = txt ? reinterpret_cast<const char *>(txt) : "";
        record.freq  = sqlite3_column_int(get_prev_stmt_, 1);
        record.mode  = sqlite3_column_int(get_prev_stmt_, 2);
        return {record, SUCCESS};
    }
    if (rc == SQLITE_DONE) {
        LV_LOG_WARN("No prev digital mode for type=%i, freq=%i", type, current_freq);
        return {Record{}, NOT_FOUND};
    }
    LV_LOG_WARN("find_prev failed: %s", sqlite3_errmsg(db_));
    return {Record{}, rc};
}

// ---------------------------------------------------------------------------
// Global database entry points
// ---------------------------------------------------------------------------

extern "C" void cfg_db_init(sqlite3 *database) {
    bool ok;
    ok = ParamsTable::Init(database);
    if (!ok) exit(1);
    ok = BandsTable::Init(database);
    if (!ok) exit(1);
    ok = BandParamsTable::Init(database);
    if (!ok) exit(1);
    ok = ModeParamsTable::Init(database);
    if (!ok) exit(1);
    ok = TransverterTable::Init(database);
    if (!ok) exit(1);
    ok = MemoryTable::Init(database);
    if (!ok) exit(1);
    ok = DigitalModesTable::Init(database);
    if (!ok) exit(1);
}

void cfg_db_shutdown() {
    ParamsTable::Shutdown();
    BandsTable::Shutdown();
    BandParamsTable::Shutdown();
    ModeParamsTable::Shutdown();
    TransverterTable::Shutdown();
    MemoryTable::Shutdown();
    DigitalModesTable::Shutdown();
}
