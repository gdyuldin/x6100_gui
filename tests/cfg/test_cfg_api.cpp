// test_cfg_api.cpp
// Tests for the C-compatible SettingsManager API (cfg_api): globals wiring,
// typed accessors with validator + deferred-write, the computed front-panel
// frequency, and immediate/delayed subscriptions against an in-memory SQLite
// database.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <vector>

#include "cfg_api.h"
#include "computed_api.h"
#include "db.h"
#include "lvgl.h"
#include "parameter.h"
#include "settings_manager.h"
#include "storage_policy.h"


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
    static void          cb(Subject * /*subj*/, void *user_data) {
        static_cast<IntObserver *>(user_data)->values.push_back(param_i_get(cfg_volume));
    }
};

// Prime the global `band_id` param and a bands row so cfg_api_init (which now
// derives the starting band from the persisted global band_id) lands on band
// `band_id` instead of the default.
void prime_band(sqlite3 *db, int band_id) {
    REQUIRE(storage_policy_for(StorageType::GLOBAL).save_int(0, "band_id", band_id) == SUCCESS);
    char *err = nullptr;
    REQUIRE(sqlite3_exec(db,
                         "INSERT INTO bands(id, name, start_freq, stop_freq, type) "
                         "VALUES(5, 'test', 5000000, 15000000, 1);",
                         nullptr, nullptr, &err) == SQLITE_OK);
}

} // namespace

TEST_CASE("cfg_api_init wires the extern globals and loads preseeded values", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    storage_policy_for(StorageType::GLOBAL).save_int(0, "vol", 55);

    cfg_api_init(nullptr);

    REQUIRE(cfg_volume != nullptr);
    REQUIRE(cfg_squelch != nullptr);
    REQUIRE(cfg_rfgain != nullptr);
    REQUIRE(cfg_rit != nullptr);
    REQUIRE(cfg_xit != nullptr);
    REQUIRE(cfg_band_vfoa_freq != nullptr);
    REQUIRE(cfg_band_vfob_freq != nullptr);
    REQUIRE(cfg_band_current_vfo != nullptr);
    REQUIRE(cfg_mode_freq_step != nullptr);
    REQUIRE(cfg_fg_freq != nullptr);

    // Preseeded DB value is loaded through the C accessor.
    REQUIRE(param_i_get(cfg_volume) == 55);
    // A key with no DB row keeps its construction-time default.
    REQUIRE(param_i_get(cfg_mode_freq_step) == 500);
}

TEST_CASE("cfg_api_init wires the extended global param handles", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    storage_policy_for(StorageType::GLOBAL).save_int(0, "key_tone", 800);
    storage_policy_for(StorageType::GLOBAL).save_int(0, "vox_delay", 700);
    storage_policy_for(StorageType::GLOBAL).save_int(0, "swrscan_span", 300000);

    cfg_api_init(nullptr);

    // Int globals.
    REQUIRE(cfg_band_id != nullptr);
    REQUIRE(cfg_mic != nullptr);
    REQUIRE(cfg_hmic != nullptr);
    REQUIRE(cfg_imic != nullptr);
    REQUIRE(cfg_moni != nullptr);
    REQUIRE(cfg_ant_id != nullptr);
    REQUIRE(cfg_atu_enabled != nullptr);
    REQUIRE(cfg_key_tone != nullptr);
    REQUIRE(cfg_key_speed != nullptr);
    REQUIRE(cfg_key_mode != nullptr);
    REQUIRE(cfg_iambic_mode != nullptr);
    REQUIRE(cfg_key_vol != nullptr);
    REQUIRE(cfg_key_train != nullptr);
    REQUIRE(cfg_qsk_time != nullptr);
    REQUIRE(cfg_cw_peak_on != nullptr);
    REQUIRE(cfg_cw_peak_q != nullptr);
    REQUIRE(cfg_cw_decoder != nullptr);
    REQUIRE(cfg_cw_tune != nullptr);
    REQUIRE(cfg_agc_hang != nullptr);
    REQUIRE(cfg_agc_knee != nullptr);
    REQUIRE(cfg_agc_slope != nullptr);
    REQUIRE(cfg_dnf != nullptr);
    REQUIRE(cfg_dnf_center != nullptr);
    REQUIRE(cfg_dnf_width != nullptr);
    REQUIRE(cfg_dnf_auto != nullptr);
    REQUIRE(cfg_nb != nullptr);
    REQUIRE(cfg_nb_level != nullptr);
    REQUIRE(cfg_nb_width != nullptr);
    REQUIRE(cfg_nr != nullptr);
    REQUIRE(cfg_nr_level != nullptr);
    REQUIRE(cfg_comp != nullptr);
    REQUIRE(cfg_fm_emphasis != nullptr);
    REQUIRE(cfg_tx_filter_low != nullptr);
    REQUIRE(cfg_tx_filter_high != nullptr);
    REQUIRE(cfg_cessb_on != nullptr);
    REQUIRE(cfg_auto_level_enabled != nullptr);
    REQUIRE(cfg_knob_info != nullptr);
    REQUIRE(cfg_vox_on != nullptr);
    REQUIRE(cfg_vox_gain != nullptr);
    REQUIRE(cfg_vox_ag != nullptr);
    REQUIRE(cfg_vox_delay != nullptr);
    REQUIRE(cfg_ft8_show_all != nullptr);
    REQUIRE(cfg_ft8_protocol != nullptr);
    REQUIRE(cfg_ft8_auto != nullptr);
    REQUIRE(cfg_ft8_hold_freq != nullptr);
    REQUIRE(cfg_ft8_max_repeats != nullptr);
    REQUIRE(cfg_swrscan_linear != nullptr);
    REQUIRE(cfg_swrscan_span != nullptr);

    // Float globals.
    REQUIRE(cfg_key_ratio != nullptr);
    REQUIRE(cfg_cw_decoder_snr != nullptr);
    REQUIRE(cfg_cw_decoder_snr_gist != nullptr);
    REQUIRE(cfg_comp_threshold_offset != nullptr);
    REQUIRE(cfg_comp_makeup_offset != nullptr);
    REQUIRE(cfg_output_gain != nullptr);
    REQUIRE(cfg_cessb_power_up != nullptr);
    REQUIRE(cfg_auto_level_offset != nullptr);

    // Text global.
    REQUIRE(cfg_encoder_bind != nullptr);

    // Band globals.
    REQUIRE(cfg_band_grid_min != nullptr);
    REQUIRE(cfg_band_grid_max != nullptr);
    REQUIRE(cfg_band_split != nullptr);
    REQUIRE(cfg_band_tx_i_offset != nullptr);
    REQUIRE(cfg_band_tx_q_offset != nullptr);
    REQUIRE(cfg_band_vfoa_mode != nullptr);
    REQUIRE(cfg_band_vfob_mode != nullptr);
    REQUIRE(cfg_band_vfoa_att != nullptr);
    REQUIRE(cfg_band_vfob_att != nullptr);
    REQUIRE(cfg_band_vfoa_pre != nullptr);
    REQUIRE(cfg_band_vfob_pre != nullptr);
    REQUIRE(cfg_band_vfoa_agc != nullptr);
    REQUIRE(cfg_band_vfob_agc != nullptr);

    // Computed globals.
    REQUIRE(cfg_cur_mode != nullptr);
    REQUIRE(cfg_cur_agc != nullptr);
    REQUIRE(cfg_cur_att != nullptr);
    REQUIRE(cfg_cur_pre != nullptr);
    REQUIRE(cfg_bg_freq != nullptr);
    REQUIRE(cfg_cur_filter_low != nullptr);
    REQUIRE(cfg_cur_filter_high != nullptr);
    REQUIRE(cfg_cur_filter_bw != nullptr);

    // Preseeded values load through the accessors.
    REQUIRE(param_i_get(cfg_key_tone) == 800);
    REQUIRE(param_i_get(cfg_vox_delay) == 700);
    REQUIRE(param_i_get(cfg_swrscan_span) == 300000);
    // Float default.
    REQUIRE(param_f_get(cfg_output_gain) == 0.0f);
    // Text global is wired (borrowed pointer semantics tested elsewhere).
    REQUIRE(cfg_encoder_bind != nullptr);
    REQUIRE(cfg_sm.p_encoder_bind.get().size() > 0);
}

TEST_CASE("cfg_api set runs the validator and persists via flush", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    cfg_api_init(nullptr);

    // 150 is outside the 0..55 volume range -> clamped by the validator.
    // Set a distinct low value first so the clamped change is detected even if
    // a prior test in the same binary left the global volume at 55.
    param_i_set(cfg_volume, 10);
    param_i_set(cfg_volume, 150);
    REQUIRE(param_i_get(cfg_volume) == 55);

    // The value is queued, not yet in the DB.
    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "vol") != 55);

    cfg_api_flush_all();

    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "vol") == 55);
}

TEST_CASE("cfg_fg_freq mirrors the active VFO and writes back through reverse fn", "[cfg_api]") {
    TestDbGuard db;
    prime_band(db.db, 5);
    StoragePolicy &b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_freq", 7'100'000) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", x6100_mode_usb) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    // Persisted band_id must also be present in the DB (global param).
    REQUIRE(storage_policy_for(StorageType::GLOBAL).save_int(0, "band_id", 5) == SUCCESS);

    cfg_api_init(nullptr);

    // Active VFO = A (0) -> front-panel freq is vfoa_freq.
    REQUIRE(cparam_i_get(cfg_fg_freq) == 7'100'000);

    // Reverse set writes back into the active VFO's freq.
    cparam_i_set(cfg_fg_freq, 7'400'000);
    REQUIRE(cparam_i_get(cfg_fg_freq) == 7'400'000);
    REQUIRE(param_i_get(cfg_band_vfoa_freq) == 7'400'000);
}
