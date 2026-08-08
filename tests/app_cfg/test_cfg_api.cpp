// test_cfg_api.cpp
// Tests for the C-compatible SettingsManager API (cfg_api): globals wiring,
// typed accessors with validator + deferred-write, the computed front-panel
// frequency, and immediate/delayed subscriptions against an in-memory SQLite
// database.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <vector>

#include "db.h"
#include "parameter.h"
#include "storage_policy.h"
#include "cfg_api.h"
#include "computed_api.h"
#include "settings_manager.h"
#include "lvgl.h"

namespace {

// In-memory DB fixture mirroring the production schema (params, band_params,
// mode_params, bands). This test process owns DB opening + table Init; the
// cfg_api does not call cfg_db_init.
struct TestDbGuard {
    sqlite3* db = nullptr;

    TestDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char* err = nullptr;
        int rc = sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS params ("
            "  name TEXT PRIMARY KEY,"
            "  val  INTEGER"
            ");"
            "CREATE TABLE IF NOT EXISTS band_params("
            "  bands_id INTEGER,"
            "  name     TEXT,"
            "  val      INTEGER,"
            "  UNIQUE (bands_id, name) ON CONFLICT REPLACE"
            ");"
            "CREATE TABLE IF NOT EXISTS mode_params("
            "  mode INTEGER,"
            "  name TEXT,"
            "  val  INTEGER,"
            "  UNIQUE (mode, name) ON CONFLICT REPLACE"
            ");"
            "CREATE TABLE IF NOT EXISTS bands("
            "  id         INTEGER PRIMARY KEY,"
            "  name       TEXT,"
            "  start_freq INTEGER,"
            "  stop_freq  INTEGER,"
            "  type       INTEGER"
            ");", nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err) sqlite3_free(err);
        ParamsTable::Init(db);
        BandParamsTable::Init(db);
        ModeParamsTable::Init(db);
        BandsTable::Init(db);
    }

    ~TestDbGuard() {
        BandsTable::Shutdown();
        ParamsTable::Shutdown();
        BandParamsTable::Shutdown();
        ModeParamsTable::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

struct IntObserver {
    std::vector<int32_t> values;
    static void cb(ParamInt* p, void* user_data)
    {
        static_cast<IntObserver*>(user_data)->values.push_back(param_int_get(p));
    }
};

// Prime the global `band_id` param and a bands row so cfg_api_init (which now
// derives the starting band from the persisted global band_id) lands on band
// `band_id` instead of the default.
void prime_band(sqlite3* db, int band_id)
{
    REQUIRE(storage_policy_for(StorageType::GLOBAL).save_int(0, "band_id", band_id) == SUCCESS);
    char* err = nullptr;
    REQUIRE(sqlite3_exec(db,
                "INSERT INTO bands(id, name, start_freq, stop_freq, type) "
                "VALUES(5, 'test', 5000000, 15000000, 1);",
                nullptr, nullptr, &err) == SQLITE_OK);
}

} // namespace

TEST_CASE("cfg_api_init wires the extern globals and loads preseeded values", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    storage_policy_for(StorageType::GLOBAL).save_int(0, "volume", 55);

    cfg_api_init(nullptr);

    REQUIRE(cfg_volume != nullptr);
    REQUIRE(cfg_squelch != nullptr);
    REQUIRE(cfg_rfgain != nullptr);
    REQUIRE(cfg_rit != nullptr);
    REQUIRE(cfg_xit != nullptr);
    REQUIRE(cfg_band_vfoa_freq != nullptr);
    REQUIRE(cfg_band_vfob_freq != nullptr);
    REQUIRE(cfg_band_current_vfo != nullptr);
    REQUIRE(cfg_mode_squelch != nullptr);
    REQUIRE(cfg_mode_agc != nullptr);
    REQUIRE(cfg_fg_freq != nullptr);

    // Preseeded DB value is loaded through the C accessor.
    REQUIRE(param_int_get(cfg_volume) == 55);
    // A key with no DB row keeps its construction-time default.
    REQUIRE(param_int_get(cfg_mode_squelch) == 0);
}

TEST_CASE("cfg_api set runs the validator and persists via flush", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    // 150 is outside the 0..100 volume range -> clamped by the validator.
    param_int_set(cfg_volume, 150);
    REQUIRE(param_int_get(cfg_volume) == 100);

    // The value is queued, not yet in the DB.
    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") != 100);

    cfg_api_flush_all();

    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") == 100);
}

TEST_CASE("cfg_fg_freq mirrors the active VFO and writes back through reverse fn", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_freq", 7'100'000) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", x6100_mode_usb) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    // Persisted band_id must also be present in the DB (global param).
    REQUIRE(storage_policy_for(StorageType::GLOBAL).save_int(0, "band_id", 5) == SUCCESS);

    cfg_api_init(nullptr);

    // Active VFO = A (0) -> front-panel freq is vfoa_freq.
    REQUIRE(computed_param_int_get(cfg_fg_freq) == 7'100'000);

    // Reverse set writes back into the active VFO's freq.
    computed_param_int_set(cfg_fg_freq, 7'400'000);
    REQUIRE(computed_param_int_get(cfg_fg_freq) == 7'400'000);
    REQUIRE(param_int_get(cfg_band_vfoa_freq) == 7'400'000);
}

TEST_CASE("cfg_api immediate subscribe fires and unsubscribes", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    IntObserver obs;
    Observer* o = param_int_subscribe(cfg_volume, IntObserver::cb, &obs);
    REQUIRE(o != nullptr);

    obs.values.clear();
    param_int_set(cfg_volume, 77);
    REQUIRE(obs.values.size() == 1);
    REQUIRE(obs.values[0] == 77);

    // After unsubscribe the observer must not be called again.
    param_unsubscribe(o);
    param_int_set(cfg_volume, 78);
    REQUIRE(obs.values.size() == 1);
}

TEST_CASE("cfg_api delayed subscribe coalesces into one latest callback", "[cfg_api][delayed]") {
    lv_init();
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    IntObserver obs;
    ObserverDelayed* o = param_int_subscribe_delayed(cfg_volume, IntObserver::cb, &obs);
    REQUIRE(o != nullptr);

    param_int_set(cfg_volume, 1);
    param_int_set(cfg_volume, 2);
    param_int_set(cfg_volume, 3);

    // Nothing delivered until the event loop runs.
    REQUIRE(obs.values.empty());

    lv_timer_handler();

    // Exactly one callback with the latest value.
    REQUIRE(obs.values == std::vector<int32_t>{3});
    param_unsubscribe(o);
}

TEST_CASE("cfg_api delayed unsubscribe cancels a pending delivery", "[cfg_api][delayed]") {
    lv_init();
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    IntObserver obs;
    ObserverDelayed* o = param_int_subscribe_delayed(cfg_volume, IntObserver::cb, &obs);

    param_int_set(cfg_volume, 5);
    param_unsubscribe(o);

    lv_timer_handler();
    REQUIRE(obs.values.empty());
}
