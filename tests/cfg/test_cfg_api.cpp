// test_cfg_api.cpp
// Tests for the C-compatible SettingsManager API (cfg_api): globals wiring,
// typed accessors with validator + deferred-write, the computed front-panel
// frequency, and immediate/delayed subscriptions against an in-memory SQLite
// database.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <vector>

#include "cfg_api.h"
#include "db.h"
#include "lvgl.h"
#include "parameter.h"
#include "settings_manager.h"


namespace {

// In-memory DB fixture mirroring the production schema (params, band_params,
// mode_params, bands). This test process owns DB opening + table Init; the
// cfg_api does not call cfg_db_init.
struct TestDbGuard {
    sqlite3 *db = nullptr;

    TestDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char *err = nullptr;
        int   rc  = sqlite3_exec(db,
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
                                    ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
        KeyValueTable<StorageType::GLOBAL>::Init(db);
        KeyValueTable<StorageType::BAND>::Init(db);
        KeyValueTable<StorageType::MODE>::Init(db);
        BandsTable::Init(db);
    }

    ~TestDbGuard() {
        BandsTable::Shutdown();
        KeyValueTable<StorageType::GLOBAL>::Shutdown();
        KeyValueTable<StorageType::BAND>::Shutdown();
        KeyValueTable<StorageType::MODE>::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

struct IntObserver {
    std::vector<int32_t> values;
    static void          cb(Subject * /*subj*/, void *user_data) {
        static_cast<IntObserver *>(user_data)->values.push_back(param_i_get(cfg.volume()));
    }
};

// Prime the global `band_id` param and a bands row so cfg_api_init (which now
// derives the starting band from the persisted global band_id) lands on band
// `band_id` instead of the default.
void prime_band(sqlite3 *db, int band_id) {
    REQUIRE(store_save<int32_t>(StorageType::GLOBAL, 0, "band_id", band_id) == SUCCESS);
    char *err = nullptr;
    REQUIRE(sqlite3_exec(db,
                         "INSERT INTO bands(id, name, start_freq, stop_freq, type) "
                         "VALUES(5, 'test', 5000000, 15000000, 1);",
                         nullptr, nullptr, &err) == SQLITE_OK);
}

} // namespace

TEST_CASE("cfg_api_init loads preseeded values through the accessors", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    store_save<int32_t>(StorageType::GLOBAL, 0, "vol", 55);

    cfg_api_init(nullptr);

    REQUIRE(cfg.volume() != nullptr);
    REQUIRE(cfg.squelch() != nullptr);
    REQUIRE(cfg.rfgain() != nullptr);
    REQUIRE(cfg.rit() != nullptr);
    REQUIRE(cfg.xit() != nullptr);
    REQUIRE(cfg.band.vfoa_freq() != nullptr);
    REQUIRE(cfg.band.vfob_freq() != nullptr);
    REQUIRE(cfg.band.current_vfo() != nullptr);
    REQUIRE(cfg.mode.freq_step() != nullptr);
    REQUIRE(cfg.cur.fg_freq() != nullptr);

    // Preseeded DB value is loaded through the C accessor.
    REQUIRE(param_i_get(cfg.volume()) == 55);
    // A key with no DB row keeps its construction-time default.
    REQUIRE(param_i_get(cfg.mode.freq_step()) == 500);
}

TEST_CASE("cfg accessor tree exposes the parameter handles", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    REQUIRE(cfg.volume() != nullptr);
    REQUIRE(cfg.band.vfoa_freq() != nullptr);
    REQUIRE(cfg.filter.high() != nullptr);
    REQUIRE(cfg.cur.mode() != nullptr);
    REQUIRE(cfg.transverter.t0_from() != nullptr);

    // In C++ the returned handle supports the Parameter<T> methods, and the
    // tree and the generic C functions address the same parameter.
    cfg.volume()->set(42);
    REQUIRE(cfg.volume()->get() == 42);
    REQUIRE(param_i_get(cfg.volume()) == 42);
}

TEST_CASE("parameter accessors are available for the extended global params", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    store_save<int32_t>(StorageType::GLOBAL, 0, "key_tone", 800);
    store_save<int32_t>(StorageType::GLOBAL, 0, "vox_delay", 700);
    store_save<int32_t>(StorageType::GLOBAL, 0, "swrscan_span", 300000);

    cfg_api_init(nullptr);

    // Int globals.
    REQUIRE(cfg.band_id() != nullptr);
    REQUIRE(cfg.mic() != nullptr);
    REQUIRE(cfg.hmic() != nullptr);
    REQUIRE(cfg.imic() != nullptr);
    REQUIRE(cfg.moni() != nullptr);
    REQUIRE(cfg.ant_id() != nullptr);
    REQUIRE(cfg.atu_enabled() != nullptr);
    REQUIRE(cfg.cw.key_tone() != nullptr);
    REQUIRE(cfg.cw.key_speed() != nullptr);
    REQUIRE(cfg.cw.key_mode() != nullptr);
    REQUIRE(cfg.cw.iambic_mode() != nullptr);
    REQUIRE(cfg.cw.key_vol() != nullptr);
    REQUIRE(cfg.cw.key_train() != nullptr);
    REQUIRE(cfg.cw.qsk_time() != nullptr);
    REQUIRE(cfg.cw.peak_on() != nullptr);
    REQUIRE(cfg.cw.peak_q() != nullptr);
    REQUIRE(cfg.cw.decoder() != nullptr);
    REQUIRE(cfg.cw.tune() != nullptr);
    REQUIRE(cfg.agc.hang() != nullptr);
    REQUIRE(cfg.agc.knee() != nullptr);
    REQUIRE(cfg.agc.slope() != nullptr);
    REQUIRE(cfg.dsp.dnf() != nullptr);
    REQUIRE(cfg.dsp.dnf_center() != nullptr);
    REQUIRE(cfg.dsp.dnf_width() != nullptr);
    REQUIRE(cfg.dsp.dnf_auto() != nullptr);
    REQUIRE(cfg.dsp.nb() != nullptr);
    REQUIRE(cfg.dsp.nb_level() != nullptr);
    REQUIRE(cfg.dsp.nb_width() != nullptr);
    REQUIRE(cfg.dsp.nr() != nullptr);
    REQUIRE(cfg.dsp.nr_level() != nullptr);
    REQUIRE(cfg.dsp.comp() != nullptr);
    REQUIRE(cfg.dsp.fm_emphasis() != nullptr);
    REQUIRE(cfg.dsp.tx_filter_low() != nullptr);
    REQUIRE(cfg.dsp.tx_filter_high() != nullptr);
    REQUIRE(cfg.dsp.cessb_on() != nullptr);
    REQUIRE(cfg.ui.auto_level_enabled() != nullptr);
    REQUIRE(cfg.ui.knob_info() != nullptr);
    REQUIRE(cfg.vox.on() != nullptr);
    REQUIRE(cfg.vox.gain() != nullptr);
    REQUIRE(cfg.vox.ag() != nullptr);
    REQUIRE(cfg.vox.delay() != nullptr);
    REQUIRE(cfg.ft8.show_all() != nullptr);
    REQUIRE(cfg.ft8.protocol() != nullptr);
    REQUIRE(cfg.ft8.auto_mode() != nullptr);
    REQUIRE(cfg.ft8.hold_freq() != nullptr);
    REQUIRE(cfg.ft8.max_repeats() != nullptr);
    REQUIRE(cfg.swrscan.linear() != nullptr);
    REQUIRE(cfg.swrscan.span() != nullptr);

    // Float globals.
    REQUIRE(cfg.cw.key_ratio() != nullptr);
    REQUIRE(cfg.cw.decoder_snr() != nullptr);
    REQUIRE(cfg.cw.decoder_snr_gist() != nullptr);
    REQUIRE(cfg.dsp.comp_threshold_offset() != nullptr);
    REQUIRE(cfg.dsp.comp_makeup_offset() != nullptr);
    REQUIRE(cfg.dsp.output_gain() != nullptr);
    REQUIRE(cfg.dsp.cessb_power_up() != nullptr);
    REQUIRE(cfg.ui.auto_level_offset() != nullptr);

    // Text global.
    REQUIRE(cfg.encoder.bind() != nullptr);

    // Band globals.
    REQUIRE(cfg.band.grid_min() != nullptr);
    REQUIRE(cfg.band.grid_max() != nullptr);
    REQUIRE(cfg.band.split() != nullptr);
    REQUIRE(cfg.band.tx_i_offset() != nullptr);
    REQUIRE(cfg.band.tx_q_offset() != nullptr);
    REQUIRE(cfg.band.vfoa_mode() != nullptr);
    REQUIRE(cfg.band.vfob_mode() != nullptr);
    REQUIRE(cfg.band.vfoa_att() != nullptr);
    REQUIRE(cfg.band.vfob_att() != nullptr);
    REQUIRE(cfg.band.vfoa_pre() != nullptr);
    REQUIRE(cfg.band.vfob_pre() != nullptr);
    REQUIRE(cfg.band.vfoa_agc() != nullptr);
    REQUIRE(cfg.band.vfob_agc() != nullptr);

    // Computed globals.
    REQUIRE(cfg.cur.mode() != nullptr);
    REQUIRE(cfg.cur.agc() != nullptr);
    REQUIRE(cfg.cur.att() != nullptr);
    REQUIRE(cfg.cur.pre() != nullptr);
    REQUIRE(cfg.cur.bg_freq() != nullptr);
    REQUIRE(cfg.filter.low() != nullptr);
    REQUIRE(cfg.filter.high() != nullptr);
    REQUIRE(cfg.filter.bw() != nullptr);

    // Preseeded values load through the accessors.
    REQUIRE(param_i_get(cfg.cw.key_tone()) == 800);
    REQUIRE(param_i_get(cfg.vox.delay()) == 700);
    REQUIRE(param_i_get(cfg.swrscan.span()) == 300000);
    // Float default.
    REQUIRE(param_f_get(cfg.dsp.output_gain()) == 0.0f);
    // Text global is wired (borrowed pointer semantics tested elsewhere).
    REQUIRE(cfg.encoder.bind() != nullptr);
    REQUIRE(cfg.encoder.bind()->get().size() > 0);
}

TEST_CASE("cfg_api set runs the validator and persists via flush", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    // 150 is outside the 0..55 volume range -> clamped by the validator.
    // Set a distinct low value first so the clamped change is detected even if
    // a prior test in the same binary left the global volume at 55.
    param_i_set(cfg.volume(), 10);
    param_i_set(cfg.volume(), 150);
    REQUIRE(param_i_get(cfg.volume()) == 55);

    // The value is queued, not yet in the DB.
    REQUIRE(store_load<int32_t>(StorageType::GLOBAL, 0, "vol") != 55);

    cfg_api_flush_all();

    REQUIRE(store_load<int32_t>(StorageType::GLOBAL, 0, "vol") == 55);
}

TEST_CASE("cfg.cur.fg_freq() mirrors the active VFO and writes back through reverse fn", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    REQUIRE(store_save<int32_t>(StorageType::BAND, 5, "vfoa_freq", 7'100'000) == SUCCESS);
    REQUIRE(store_save<int32_t>(StorageType::BAND, 5, "vfoa_mode", x6100_mode_usb) == SUCCESS);
    REQUIRE(store_save<int32_t>(StorageType::BAND, 5, "vfo", 0) == SUCCESS);
    // Persisted band_id must also be present in the DB (global param).
    REQUIRE(store_save<int32_t>(StorageType::GLOBAL, 0, "band_id", 5) == SUCCESS);

    cfg_api_init(nullptr);

    // Active VFO = A (0) -> front-panel freq is vfoa_freq.
    REQUIRE(cparam_i_get(cfg.cur.fg_freq()) == 7'100'000);

    // Reverse set writes back into the active VFO's freq.
    cparam_i_set(cfg.cur.fg_freq(), 7'400'000);
    REQUIRE(cparam_i_get(cfg.cur.fg_freq()) == 7'400'000);
    REQUIRE(param_i_get(cfg.band.vfoa_freq()) == 7'400'000);
}
