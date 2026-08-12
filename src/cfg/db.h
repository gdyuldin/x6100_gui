#pragma once

#include <sqlite3.h>

#ifdef __cplusplus
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "../lvgl/lvgl.h"
#endif

#ifndef BAND_UNDEFINED
#define BAND_UNDEFINED (-1)
#endif

typedef enum {
    SUCCESS     = 0,
    WRONG_TYPE  = -1,
    NOT_FOUND   = -2,
    WRONG_VALUE = -3,
} load_save_error_codes_t;

#ifdef __cplusplus

// RAII guard: locks the statement mutex for the whole statement use and
// resets/clears bindings on scope exit.
class StmtResetGuard {
    std::unique_lock<std::mutex> lock_;
    sqlite3_stmt                *stmt_;

  public:
    explicit StmtResetGuard(std::mutex &mux, sqlite3_stmt *stmt) : lock_(mux), stmt_(stmt) {}

    ~StmtResetGuard() {
        if (stmt_) {
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
    }
    // Delete copy and move constructor and assignment operator to prevent copying
    StmtResetGuard(const StmtResetGuard &)            = delete;
    StmtResetGuard &operator=(const StmtResetGuard &) = delete;
    StmtResetGuard(StmtResetGuard &&)                 = delete;
    StmtResetGuard &operator=(StmtResetGuard &&)      = delete;
};

// Helper trait that is always false, but depends on T
template <typename> inline constexpr bool always_false_v = false;

// value to std::string converter for logging
template <typename T> std::string value_to_string(const T &value) {
    if constexpr (std::is_same_v<T, int32_t>) {
        std::array<char, 12> buf{}; // enough for 32-bit int
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
        return std::string(buf.data(), ptr);
    } else if constexpr (std::is_same_v<T, float>) {
        // std::to_chars(float) requires libstdc++ >= 11; fall back to snprintf
        // to keep compatibility with older toolchains.
        std::array<char, 32> buf{};
        std::snprintf(buf.data(), buf.size(), "%g", static_cast<double>(value));
        return std::string(buf.data());
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else {
        static_assert(always_false_v<T>, "Unsupported type for logging");
    }
}

template <typename T> struct ParamLoadResult {
    T   value;
    int rc; // load_save_error_codes_t (negative) or sqlite3 rc (positive)
};

class ParamsTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    template <typename T> static ParamLoadResult<T> Load(const char *name) {
        int            rc;
        StmtResetGuard guard(load_mutex_, load_stmt_);

        rc = sqlite3_bind_text(load_stmt_, load_name_param_index_, name, strlen(name), SQLITE_STATIC);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind name %s: %s", name, sqlite3_errmsg(db_));
            return {T{}, rc};
        }

        rc = sqlite3_step(load_stmt_);
        if (rc == SQLITE_ROW) {
            T value;
            if constexpr (std::is_same_v<T, int32_t>) {
                value = sqlite3_column_int(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%i", name, value);
            } else if constexpr (std::is_same_v<T, float>) {
                value = sqlite3_column_double(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%f", name, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const unsigned char *txt = sqlite3_column_text(load_stmt_, 0);
                value                    = txt ? reinterpret_cast<const char *>(txt) : "";
                LV_LOG_USER("Loaded %s=%s", name, value.c_str());
            } else {
                static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_load().");
            }
            return {value, SUCCESS};
        }
        if (rc == SQLITE_DONE) {
            LV_LOG_WARN("No results for load %s", name);
            return {T{}, NOT_FOUND};
        }
        LV_LOG_WARN("Load %s failed: %s", name, sqlite3_errmsg(db_));
        return {T{}, rc};
    }

    template <typename T> static int Save(const char *name, const T &value) {
        int            rc;
        StmtResetGuard guard(save_mutex_, save_stmt_);

        rc = sqlite3_bind_text(save_stmt_, save_name_param_index_, name, strlen(name), 0);
        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind name %s to save params query", name);
            return rc;
        }

        if constexpr (std::is_same_v<T, int32_t>) {
            rc = sqlite3_bind_int(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, float>) {
            rc = sqlite3_bind_double(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(save_stmt_, save_val_param_index_, value.c_str(), -1, 0);
        } else {
            static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_save().");
        }

        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind val %s to save params query", value_to_string(value).c_str());
        } else {
            rc = sqlite3_step(save_stmt_);
            if (rc != SQLITE_DONE) {
                LV_LOG_ERROR("Failed save item %s: %s", name, sqlite3_errmsg(db_));
            } else {
                LV_LOG_USER("Saved %s=%s", name, value_to_string(value).c_str());
                rc = SUCCESS;
            }
        }
        return rc;
    }

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Load statement group: prepared statement + guarding mutex + cached
    // :name parameter index. Indices are resolved once at Init via
    // sqlite3_bind_parameter_index() and reused on every bind.
    inline static sqlite3_stmt *load_stmt_ = nullptr;
    inline static std::mutex    load_mutex_;
    inline static int           load_name_param_index_ = 0;

    // Save statement group: prepared statement + guarding mutex + cached
    // :name and :val parameter indices.
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_name_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;
};

enum band_type_t {
    BAND_INACTIVE = 0,
    BAND_ACTIVE   = 1,
};

struct BandInfo {
    int32_t     id = BAND_UNDEFINED;  // Band ID. 0 or positive for defined bands
    std::string name;                 // Band name
    uint32_t    start_freq;           // Start freq
    uint32_t    stop_freq;            // Stop freq (including)
    band_type_t type = BAND_INACTIVE; // active flag to filter bands during band_up/down
};

struct BandInfoLoadResult {
    BandInfo value;
    int      rc; // load_save_error_codes_t (negative) or sqlite3 rc (positive)
};

class BandsTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    static BandInfoLoadResult    get_by_id(int32_t band_id);
    static BandInfoLoadResult    get_by_freq(uint32_t freq);
    static BandInfoLoadResult    next(int32_t cur_band_id, uint32_t cur_freq, bool up);
    static std::vector<BandInfo> all_bands();

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // get_band_by_id: `SELECT ... FROM bands WHERE id = :id`.
    inline static sqlite3_stmt *get_band_by_id_stmt_ = nullptr;
    inline static std::mutex    get_band_by_id_mutex_;
    inline static int           get_band_by_id_id_param_index_ = 0;

    // get_band_by_freq: find the band containing (:freq) or the gap around it.
    inline static sqlite3_stmt *get_band_by_freq_stmt_ = nullptr;
    inline static std::mutex    get_band_by_freq_mutex_;
    inline static int           get_band_by_freq_freq_param_index_ = 0;

    // get_band_up: next band above :freq, excluding :id.
    inline static sqlite3_stmt *get_band_up_stmt_ = nullptr;
    inline static std::mutex    get_band_up_mutex_;
    inline static int           get_band_up_freq_param_index_ = 0;
    inline static int           get_band_up_id_param_index_   = 0;

    // get_band_down: previous band below :freq, excluding :id.
    inline static sqlite3_stmt *get_band_down_stmt_ = nullptr;
    inline static std::mutex    get_band_down_mutex_;
    inline static int           get_band_down_freq_param_index_ = 0;
    inline static int           get_band_down_id_param_index_   = 0;

    // read_all_bands: dump every band row.
    inline static sqlite3_stmt *read_all_bands_stmt_ = nullptr;
    inline static std::mutex    read_all_bands_mutex_;

    // Cache for last loaded band info, protected by its own mutex (the cache
    // is read/written outside the statement guard).
    inline static BandInfo   last_band;
    inline static std::mutex last_band_mutex_;
};

class BandParamsTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    template <typename T> static ParamLoadResult<T> Load(const int32_t band_id, const char *name) {
        int            rc;
        StmtResetGuard guard(load_mutex_, load_stmt_);

        rc = sqlite3_bind_text(load_stmt_, load_name_param_index_, name, strlen(name), SQLITE_STATIC);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind name %s: %s", name, sqlite3_errmsg(db_));
            return {T{}, rc};
        }
        rc = sqlite3_bind_int(load_stmt_, load_id_param_index_, band_id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db_));
            return {T{}, rc};
        }

        rc = sqlite3_step(load_stmt_);
        if (rc == SQLITE_ROW) {
            T value;
            if constexpr (std::is_same_v<T, int32_t>) {
                value = sqlite3_column_int(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%i", name, value);
            } else if constexpr (std::is_same_v<T, float>) {
                value = sqlite3_column_double(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%f", name, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const unsigned char *txt = sqlite3_column_text(load_stmt_, 0);
                value                    = txt ? reinterpret_cast<const char *>(txt) : "";
                LV_LOG_USER("Loaded %s=%s", name, value.c_str());
            } else {
                static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_load().");
            }
            return {value, SUCCESS};
        }
        if (rc == SQLITE_DONE) {
            LV_LOG_WARN("No results for load %s", name);
            return {T{}, NOT_FOUND};
        }
        LV_LOG_WARN("Load %s failed: %s", name, sqlite3_errmsg(db_));
        return {T{}, rc};
    }

    template <typename T> static int Save(const int32_t band_id, const char *name, const T &value) {
        int            rc;
        StmtResetGuard guard(save_mutex_, save_stmt_);

        rc = sqlite3_bind_text(save_stmt_, save_name_param_index_, name, strlen(name), 0);
        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind name %s to save params query", name);
            return rc;
        }
        rc = sqlite3_bind_int(save_stmt_, save_id_param_index_, band_id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db_));
            return rc;
        }

        if constexpr (std::is_same_v<T, int32_t>) {
            rc = sqlite3_bind_int(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, float>) {
            rc = sqlite3_bind_double(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(save_stmt_, save_val_param_index_, value.c_str(), -1, 0);
        } else {
            static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_save().");
        }

        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind val %s to save params query", value_to_string(value).c_str());
        } else {
            rc = sqlite3_step(save_stmt_);
            if (rc != SQLITE_DONE) {
                LV_LOG_ERROR("Failed save item %s: %s", name, sqlite3_errmsg(db_));
            } else {
                LV_LOG_USER("Saved %s=%s", name, value_to_string(value).c_str());
                rc = SUCCESS;
            }
        }
        return rc;
    }

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Load statement group: prepared statement + guarding mutex + cached
    // :id and :name parameter indices.
    inline static sqlite3_stmt *load_stmt_ = nullptr;
    inline static std::mutex    load_mutex_;
    inline static int           load_id_param_index_   = 0;
    inline static int           load_name_param_index_ = 0;

    // Save statement group: prepared statement + guarding mutex + cached
    // :id, :name and :val parameter indices.
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_id_param_index_   = 0;
    inline static int           save_name_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;
};

class ModeParamsTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    template <typename T> static ParamLoadResult<T> Load(const int32_t band_id, const char *name) {
        int            rc;
        StmtResetGuard guard(load_mutex_, load_stmt_);

        rc = sqlite3_bind_text(load_stmt_, load_name_param_index_, name, strlen(name), SQLITE_STATIC);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind name %s: %s", name, sqlite3_errmsg(db_));
            return {T{}, rc};
        }
        rc = sqlite3_bind_int(load_stmt_, load_id_param_index_, band_id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db_));
            return {T{}, rc};
        }

        rc = sqlite3_step(load_stmt_);
        if (rc == SQLITE_ROW) {
            T value;
            if constexpr (std::is_same_v<T, int32_t>) {
                value = sqlite3_column_int(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%i", name, value);
            } else if constexpr (std::is_same_v<T, float>) {
                value = sqlite3_column_double(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%f", name, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const unsigned char *txt = sqlite3_column_text(load_stmt_, 0);
                value                    = txt ? reinterpret_cast<const char *>(txt) : "";
                LV_LOG_USER("Loaded %s=%s", name, value.c_str());
            } else {
                static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_load().");
            }
            return {value, SUCCESS};
        }
        if (rc == SQLITE_DONE) {
            LV_LOG_WARN("No results for load %s", name);
            return {T{}, NOT_FOUND};
        }
        LV_LOG_WARN("Load %s failed: %s", name, sqlite3_errmsg(db_));
        return {T{}, rc};
    }

    template <typename T> static int Save(const int32_t band_id, const char *name, const T &value) {
        int            rc;
        StmtResetGuard guard(save_mutex_, save_stmt_);

        rc = sqlite3_bind_text(save_stmt_, save_name_param_index_, name, strlen(name), 0);
        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind name %s to save params query", name);
            return rc;
        }
        rc = sqlite3_bind_int(save_stmt_, save_id_param_index_, band_id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db_));
            return rc;
        }

        if constexpr (std::is_same_v<T, int32_t>) {
            rc = sqlite3_bind_int(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, float>) {
            rc = sqlite3_bind_double(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(save_stmt_, save_val_param_index_, value.c_str(), -1, 0);
        } else {
            static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_save().");
        }

        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind val %s to save params query", value_to_string(value).c_str());
        } else {
            rc = sqlite3_step(save_stmt_);
            if (rc != SQLITE_DONE) {
                LV_LOG_ERROR("Failed save item %s: %s", name, sqlite3_errmsg(db_));
            } else {
                LV_LOG_USER("Saved %s=%s", name, value_to_string(value).c_str());
                rc = SUCCESS;
            }
        }
        return rc;
    }

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Load statement group: prepared statement + guarding mutex + cached
    // :id and :name parameter indices.
    inline static sqlite3_stmt *load_stmt_ = nullptr;
    inline static std::mutex    load_mutex_;
    inline static int           load_id_param_index_   = 0;
    inline static int           load_name_param_index_ = 0;

    // Save statement group: prepared statement + guarding mutex + cached
    // :id, :name and :val parameter indices.
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_id_param_index_   = 0;
    inline static int           save_name_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;
};

// Transverter configuration: (transverter_id, name, val) rows in the legacy
// `transverter` SQLite table. `id` is the transverter number (0 or 1), `name`
// is "from"/"to"/"shift" and `val` is a frequency in Hz (int32_t). Uses the
// exact legacy column names and UNIQUE(id, name) constraint so existing user
// data survives migration. Follows the BandParamsTable pattern; context_id in
// StorageKey maps to `id` (fixed for the parameter's lifetime — never switched).
class TransverterTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    template <typename T> static ParamLoadResult<T> Load(const int32_t id, const char *name) {
        int            rc;
        StmtResetGuard guard(load_mutex_, load_stmt_);

        rc = sqlite3_bind_text(load_stmt_, load_name_param_index_, name, strlen(name), SQLITE_STATIC);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind name %s: %s", name, sqlite3_errmsg(db_));
            return {T{}, rc};
        }
        rc = sqlite3_bind_int(load_stmt_, load_id_param_index_, id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind transverter id %i: %s", id, sqlite3_errmsg(db_));
            return {T{}, rc};
        }

        rc = sqlite3_step(load_stmt_);
        if (rc == SQLITE_ROW) {
            T value;
            if constexpr (std::is_same_v<T, int32_t>) {
                value = sqlite3_column_int(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%i", name, value);
            } else if constexpr (std::is_same_v<T, float>) {
                value = sqlite3_column_double(load_stmt_, 0);
                LV_LOG_USER("Loaded %s=%f", name, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const unsigned char *txt = sqlite3_column_text(load_stmt_, 0);
                value                    = txt ? reinterpret_cast<const char *>(txt) : "";
                LV_LOG_USER("Loaded %s=%s", name, value.c_str());
            } else {
                static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_load().");
            }
            return {value, SUCCESS};
        }
        if (rc == SQLITE_DONE) {
            LV_LOG_WARN("No results for load %s", name);
            return {T{}, NOT_FOUND};
        }
        LV_LOG_WARN("Load %s failed: %s", name, sqlite3_errmsg(db_));
        return {T{}, rc};
    }

    template <typename T> static int Save(const int32_t id, const char *name, const T &value) {
        int            rc;
        StmtResetGuard guard(save_mutex_, save_stmt_);

        rc = sqlite3_bind_text(save_stmt_, save_name_param_index_, name, strlen(name), 0);
        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind name %s to save params query", name);
            return rc;
        }
        rc = sqlite3_bind_int(save_stmt_, save_id_param_index_, id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind transverter id %i: %s", id, sqlite3_errmsg(db_));
            return rc;
        }

        if constexpr (std::is_same_v<T, int32_t>) {
            rc = sqlite3_bind_int(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, float>) {
            rc = sqlite3_bind_double(save_stmt_, save_val_param_index_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(save_stmt_, save_val_param_index_, value.c_str(), -1, 0);
        } else {
            static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_save().");
        }

        if (rc != SQLITE_OK) {
            LV_LOG_WARN("Can't bind val %s to save params query", value_to_string(value).c_str());
        } else {
            rc = sqlite3_step(save_stmt_);
            if (rc != SQLITE_DONE) {
                LV_LOG_ERROR("Failed save item %s: %s", name, sqlite3_errmsg(db_));
            } else {
                LV_LOG_USER("Saved %s=%s", name, value_to_string(value).c_str());
                rc = SUCCESS;
            }
        }
        return rc;
    }

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Load statement group: prepared statement + guarding mutex + cached
    // :id and :name parameter indices.
    inline static sqlite3_stmt *load_stmt_ = nullptr;
    inline static std::mutex    load_mutex_;
    inline static int           load_id_param_index_   = 0;
    inline static int           load_name_param_index_ = 0;

    // Save statement group: prepared statement + guarding mutex + cached
    // :id, :name and :val parameter indices.
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_id_param_index_   = 0;
    inline static int           save_name_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;
};

// Key-value snapshot store for user memory slots (hardware-key memories,
// backup slot). NOT a deferred-write parameter store: saves are immediate and
// loads are done on demand. Each row is (id, name, value); a "slot" is all
// rows sharing the same id, following the legacy `memory` table schema for
// compatibility with existing user configurations. Field names match the old
// cfg module: vfoa_freq, vfoa_mode, vfoa_agc, vfoa_pre, vfoa_att.
class MemoryTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    // Save a single (id, name, val) row. INSERT OR REPLACE makes repeated
    // saves of the same name for the same id overwrite the previous value.
    static int Save(int32_t id, const char *name, int32_t value);

    // Reads all rows for the given id and fills the output fields. Each field
    // has a paired `has_*` flag that is true only when a matching row was
    // found. Returns false if no vfoa_freq row exists for this id (the slot
    // is not loadable); otherwise true.
    static bool Load(int32_t id, int32_t &freq, bool &has_freq, int32_t &mode, bool &has_mode, int32_t &agc,
                     bool &has_agc, int32_t &att, bool &has_att, int32_t &pre, bool &has_pre);

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Save statement group: INSERT OR REPLACE INTO memory(id, name, val).
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_id_param_index_   = 0;
    inline static int           save_name_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;

    // Load statement group: SELECT name, val FROM memory WHERE id = :id.
    inline static sqlite3_stmt *load_stmt_ = nullptr;
    inline static std::mutex    load_mutex_;
    inline static int           load_id_param_index_ = 0;
};

// Read-only presets of digital mode working frequencies (FT8/FT4), mirroring
// the legacy `digital_modes` table from sql/params.sql. Each row carries a
// label, a frequency and the radio mode (always x6100_mode_usb_dig in the
// seed data). Navigation is directional: find_next/find_prev return the
// preset strictly above/below a frequency, find_closest the nearest one.
class DigitalModesTable {
  public:
    struct Record {
        std::string label;
        int32_t     freq;
        int32_t     mode;
    };
    struct LoadResult {
        Record value;
        int    rc; // load_save_error_codes_t (negative) or sqlite3 rc (positive)
    };

    static bool Init(sqlite3 *database);
    static void Shutdown();

    static LoadResult find_next(int32_t type, int32_t current_freq);
    static LoadResult find_closest(int32_t type, int32_t current_freq);
    static LoadResult find_prev(int32_t type, int32_t current_freq);

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // get_next: freq > :freq AND type = :type ORDER BY freq ASC LIMIT 1.
    inline static sqlite3_stmt *get_next_stmt_ = nullptr;
    inline static std::mutex    get_next_mutex_;
    inline static int           get_next_type_param_index_ = 0;
    inline static int           get_next_freq_param_index_ = 0;

    // get_closest: type = :type ORDER BY ABS(freq - :freq) ASC LIMIT 1.
    inline static sqlite3_stmt *get_closest_stmt_ = nullptr;
    inline static std::mutex    get_closest_mutex_;
    inline static int           get_closest_type_param_index_ = 0;
    inline static int           get_closest_freq_param_index_ = 0;

    // get_prev: freq < :freq AND type = :type ORDER BY freq DESC LIMIT 1.
    inline static sqlite3_stmt *get_prev_stmt_ = nullptr;
    inline static std::mutex    get_prev_mutex_;
    inline static int           get_prev_type_param_index_ = 0;
    inline static int           get_prev_freq_param_index_ = 0;
};

// Per-antenna, per-frequency tuner-network values in the legacy `atu` table
// (ant, freq, val) with UNIQUE (ant, freq). ATU does NOT fit the
// Parameter<T>/StoragePolicy/PendingWrites pipeline (composite key, bulk load,
// nearest-match scan, immediate save), so it follows the MemoryTable pattern:
// raw DB access only, with the reactive cache living in AtuNetworkCache
// (atu_cache.h). Uses the exact legacy column/table names so existing user data
// survives. Follows the MemoryTable pattern with one mutex per statement.
class AtuTable {
  public:
    static bool Init(sqlite3 *database);
    static void Shutdown();

    struct AtuEntry {
        int32_t  freq;
        uint32_t network;
    };

    // Save a single (ant, freq, val) row. INSERT OR REPLACE makes repeated
    // saves of the same (ant, freq) overwrite the previous value.
    static int Save(int32_t ant, int32_t freq, uint32_t network);

    // Delete rows for the same antenna within ±step Hz of freq (excluding the
    // exact match). Used after Save to prune adjacent stale entries.
    // Returns sqlite3 rc; SUCCESS (0) on DONE.
    static int DeleteAdjacent(int32_t ant, int32_t freq, int32_t step);

    // Load all rows for an antenna into the provided vector (cleared first),
    // in frequency ascending order. Returns SUCCESS (0) regardless of whether
    // rows exist (empty vector on no rows); returns positive sqlite3 rc on
    // error.
    static int LoadAll(int32_t ant, std::vector<AtuEntry> &out);

  private:
    // Database handle shared by all statements of this table.
    inline static sqlite3 *db_ = nullptr;

    // Save statement group: INSERT OR REPLACE INTO atu(ant, freq, val).
    inline static sqlite3_stmt *save_stmt_ = nullptr;
    inline static std::mutex    save_mutex_;
    inline static int           save_ant_param_index_  = 0;
    inline static int           save_freq_param_index_ = 0;
    inline static int           save_val_param_index_  = 0;

    // Delete-adjacent statement group: DELETE WHERE ant, freq in [freq-step,
    // freq+step], and freq != :freq.
    inline static sqlite3_stmt *delete_adjacent_stmt_ = nullptr;
    inline static std::mutex    delete_adjacent_mutex_;
    inline static int           delete_adjacent_ant_param_index_  = 0;
    inline static int           delete_adjacent_freq_param_index_ = 0;
    inline static int           delete_adjacent_step_param_index_ = 0;

    // Load-all statement group: SELECT freq, val WHERE ant ORDER BY freq ASC.
    inline static sqlite3_stmt *load_all_stmt_ = nullptr;
    inline static std::mutex    load_all_mutex_;
    inline static int           load_all_ant_param_index_ = 0;
};


#endif
#ifdef __cplusplus
extern "C" {
#endif

// Global database entry points.
void cfg_db_init(sqlite3 *database);
void cfg_db_shutdown();

#ifdef __cplusplus
}
#endif
