// test_manager.cpp
// Integration tests for SettingsManager: init load, band/mode switching
// semantics (explicit/implicit), VFO retention, deferred-write round-trip
// against an in-memory SQLite database.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <sqlite3.h>

#include <chrono>
#include <thread>

#include "db.h"
#include "settings_manager.h"
#include "tests/app_cfg/mocks/pending_writes_test_access.h"

extern "C" {
    #include <aether_radio/x6100_control/control.h>
}

namespace {

// Frequency conversion helper: express band boundaries in kHz for readability.
constexpr int32_t kHz = 1000;

// In-memory DB fixture: creates all three params tables and initialises the
// shared prepared statements of every table class used by the storage
// policies. A BAND/MODE round-trip fails with rc=21 (SQLITE_MISUSE) if
// BandParamsTable/ModeParamsTable is not initialised.
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

} // namespace

TEST_CASE("SettingsManager init_load loads global/band/mode params", "[manager]") {
    TestDbGuard db;

    // Pre-populate the DB with known values for band 5 / mode 3.
    {
        StoragePolicy& g = storage_policy_for(StorageType::GLOBAL);
        REQUIRE(g.save_int(0, "volume", 77) == SUCCESS);
        StoragePolicy& b = storage_policy_for(StorageType::BAND);
        REQUIRE(b.save_int(5, "vfoa_freq", 7100) == SUCCESS);
        REQUIRE(b.save_int(5, "vfob_freq", 14300) == SUCCESS);
        REQUIRE(b.save_int(5, "vfo", 1) == SUCCESS);
        StoragePolicy& m = storage_policy_for(StorageType::MODE);
        REQUIRE(m.save_int(3, "squelch", 9) == SUCCESS);
    }

    SettingsManager mgr;
    mgr.init_load(5, 3);

    REQUIRE(mgr.current_band_id() == 5);
    REQUIRE(mgr.current_mode_id() == 3);

    REQUIRE(mgr.p_volume.get() == 77);
    REQUIRE(mgr.p_band_vfoa_freq.get() == 7100);
    REQUIRE(mgr.p_band_vfob_freq.get() == 14300);
    REQUIRE(mgr.p_band_current_vfo.get() == 1);
    REQUIRE(mgr.p_mode_squelch.get() == 9);

    // fg_freq mirrors the active VFO (current_vfo == 1 -> vfob).
    REQUIRE(mgr.cp_fg_freq.get() == 14300);
}

TEST_CASE("SettingsManager deferred writes round-trip after flush", "[manager]") {
    TestDbGuard db;

    SettingsManager mgr;
    mgr.init_load(5, 3);

    // Change parameters; the new values go to the journal (not the DB yet).
    mgr.p_volume.set(80);
    mgr.p_band_if_shift.set(10);
    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") != 80);

    mgr.flush_all();

    // Values are now persisted.
    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") == 80);
    auto if_shift = storage_policy_for(StorageType::BAND).load_int(5, "if_shift");
    REQUIRE(if_shift.has_value());
    REQUIRE(*if_shift == 10);

    // A fresh manager loads the persisted values.
    SettingsManager mgr2;
    mgr2.init_load(5, 3);
    REQUIRE(mgr2.p_volume.get() == 80);
    REQUIRE(mgr2.p_band_if_shift.get() == 10);
}

TEST_CASE("switch_band_explicit loads new band, keeps VFO reference", "[manager]") {
    TestDbGuard db;

    // Band 5: VFO reference = A (0).
    {
        StoragePolicy& b = storage_policy_for(StorageType::BAND);
        REQUIRE(b.save_int(5, "vfoa_freq", 7100) == SUCCESS);
        REQUIRE(b.save_int(5, "vfob_freq", 14300) == SUCCESS);
        REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
        // Band 6 has different frequencies and no current_vfo row (missing).
        REQUIRE(b.save_int(6, "vfoa_freq", 10100) == SUCCESS);
        REQUIRE(b.save_int(6, "vfob_freq", 18100) == SUCCESS);
    }

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.p_band_current_vfo.get() == 0);
    REQUIRE(mgr.cp_fg_freq.get() == 7100);

    // Change VFO reference to B on the current band, then switch explicitly.
    mgr.p_band_current_vfo.set(1);
    mgr.switch_band_explicit(6);

    REQUIRE(mgr.current_band_id() == 6);
    // current_vfo is NOT loaded on explicit switch: it keeps B (1).
    REQUIRE(mgr.p_band_current_vfo.get() == 1);
    // Both frequency slots loaded from band 6.
    REQUIRE(mgr.p_band_vfoa_freq.get() == 10100);
    REQUIRE(mgr.p_band_vfob_freq.get() == 18100);
    // fg_freq = active VFO (B) frequency of band 6.
    REQUIRE(mgr.cp_fg_freq.get() == 18100);
}

TEST_CASE("switch_band_implicit keeps active VFO frequency", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_freq", 7100) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_freq", 14300) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    REQUIRE(b.save_int(6, "vfoa_freq", 3650) == SUCCESS);
    REQUIRE(b.save_int(6, "vfob_freq", 5000) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.p_band_current_vfo.get() == 0);
    REQUIRE(mgr.cp_fg_freq.get() == 7100);

    // The radio crossed into band 6 while tuning: the frequency of the active
    // VFO (A) must stay at its current value (7100 tuned, not 3650 from DB).
    mgr.p_band_vfoa_freq.set(7250);  // tune inside band 5
    mgr.switch_band_implicit(6);

    REQUIRE(mgr.p_band_vfoa_freq.get() == 7250);  // kept
    REQUIRE(mgr.p_band_vfob_freq.get() == 5000);  // inactive VFO loaded
    REQUIRE(mgr.p_band_current_vfo.get() == 0);   // VFO reference kept
    REQUIRE(mgr.cp_fg_freq.get() == 7250);
}

TEST_CASE("switch_mode saves and loads mode params", "[manager]") {
    TestDbGuard db;

    StoragePolicy& m = storage_policy_for(StorageType::MODE);
    REQUIRE(m.save_int(3, "squelch", 9) == SUCCESS);
    REQUIRE(m.save_int(4, "squelch", 15) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.p_mode_squelch.get() == 9);

    mgr.p_mode_squelch.set(11);
    mgr.switch_mode(4);

    REQUIRE(mgr.current_mode_id() == 4);
    REQUIRE(mgr.p_mode_squelch.get() == 15);
}

TEST_CASE("init_load loads vfo modes, cur_mode mirrors active VFO", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", 8) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);

    REQUIRE(mgr.p_band_vfoa_mode.get() == 3);
    REQUIRE(mgr.p_band_vfob_mode.get() == 8);
    // active VFO = A (0) -> cur_mode mirrors vfoa_mode.
    REQUIRE(mgr.cp_cur_mode.get() == 3);
}

TEST_CASE("cp_cur_mode.set writes active VFO mode and triggers switch_mode", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", 8) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    StoragePolicy& m = storage_policy_for(StorageType::MODE);
    REQUIRE(m.save_int(3, "squelch", 9) == SUCCESS);
    REQUIRE(m.save_int(4, "squelch", 15) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.cp_cur_mode.get() == 3);
    REQUIRE(mgr.current_mode_id() == 3);

    mgr.cp_cur_mode.set(4);

    // Writes back into the active (A) VFO's mode param.
    REQUIRE(mgr.p_band_vfoa_mode.get() == 4);
    REQUIRE(mgr.cp_cur_mode.get() == 4);
    // switch_mode() was triggered by the cp_cur_mode change.
    REQUIRE(mgr.current_mode_id() == 4);
    REQUIRE(mgr.p_mode_squelch.get() == 15);
}

TEST_CASE("VFO toggle changes cur_mode and triggers switch_mode", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", 7) == SUCCESS);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    StoragePolicy& m = storage_policy_for(StorageType::MODE);
    REQUIRE(m.save_int(3, "squelch", 9) == SUCCESS);
    REQUIRE(m.save_int(7, "squelch", 21) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.cp_cur_mode.get() == 3);
    REQUIRE(mgr.current_mode_id() == 3);

    // Switch active VFO to B: cur_mode now reflects vfob_mode and switch_mode
    // follows the new mode context.
    mgr.p_band_current_vfo.set(1);

    REQUIRE(mgr.cp_cur_mode.get() == 7);
    REQUIRE(mgr.current_mode_id() == 7);
    REQUIRE(mgr.p_mode_squelch.get() == 21);
}

TEST_CASE("switch_band_explicit loads both vfo modes", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", 8) == SUCCESS);
    REQUIRE(b.save_int(6, "vfoa_mode", 4) == SUCCESS);
    REQUIRE(b.save_int(6, "vfob_mode", 9) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.cp_cur_mode.get() == 3);

    mgr.switch_band_explicit(6);

    REQUIRE(mgr.p_band_current_vfo.get() == 0);  // VFO reference kept
    REQUIRE(mgr.p_band_vfoa_mode.get() == 4);
    REQUIRE(mgr.p_band_vfob_mode.get() == 9);
    REQUIRE(mgr.cp_cur_mode.get() == 4);         // active (A) VFO mode of band 6
}

TEST_CASE("switch_band_implicit keeps active VFO mode", "[manager]") {
    TestDbGuard db;

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", 8) == SUCCESS);
    REQUIRE(b.save_int(6, "vfoa_mode", 4) == SUCCESS);
    REQUIRE(b.save_int(6, "vfob_mode", 9) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    REQUIRE(mgr.cp_cur_mode.get() == 3);

    // Change the mode of the active (A) VFO, then cross bands implicitly.
    mgr.p_band_vfoa_mode.set(5);
    mgr.switch_band_implicit(6);

    REQUIRE(mgr.p_band_vfoa_mode.get() == 5);   // active VFO mode kept
    REQUIRE(mgr.p_band_vfob_mode.get() == 9);   // inactive VFO mode loaded
    REQUIRE(mgr.p_band_current_vfo.get() == 0);
    REQUIRE(mgr.cp_cur_mode.get() == 5);
}

// Helpers shared by the VFO-restore tests below.
namespace {

void insert_band(sqlite3* db, int id, int start, int stop, int type = 1)
{
    char sql[256];
    std::snprintf(sql, sizeof(sql),
                  "INSERT INTO bands(id, name, start_freq, stop_freq, type) "
                  "VALUES(%d, 'test', %d, %d, %d);", id, start, stop, type);
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}

// initialise `mgr` against an empty band_params table for `band_id`.
void fresh_manager(SettingsManager& mgr, TestDbGuard& db, int band_id)
{
    mgr.init_load(band_id, 3);
}

} // namespace

TEST_CASE("VFO restore on an empty high band (> 10 MHz)", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 5, 10100 * kHz, 10150 * kHz);

    SettingsManager mgr;
    fresh_manager(mgr, db, 5);

    // NOT_FOUND in band_params -> restore from the band definition.
    REQUIRE(mgr.p_band_vfoa_freq.get() == 10100 * kHz);
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_usb);
    // vfob copies the restored vfoa freq/mode.
    REQUIRE(mgr.p_band_vfob_freq.get() == 10100 * kHz);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_usb);
}

TEST_CASE("VFO restore picks LSB on a low band", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 1, 1400 * kHz, 2000 * kHz);

    SettingsManager mgr;
    fresh_manager(mgr, db, 1);

    REQUIRE(mgr.p_band_vfoa_freq.get() == 1400 * kHz);
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_lsb);
    REQUIRE(mgr.p_band_vfob_freq.get() == 1400 * kHz);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_lsb);
}

TEST_CASE("VFO boundary clamp of a DB value outside the band", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 5, 10100 * kHz, 10150 * kHz);

    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_freq", 999999 * kHz) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_freq", 1 * kHz) == SUCCESS);

    SettingsManager mgr;
    fresh_manager(mgr, db, 5);

    // Out-of-range loaded values are clamped to the band start.
    REQUIRE(mgr.p_band_vfoa_freq.get() == 10100 * kHz);
    REQUIRE(mgr.p_band_vfob_freq.get() == 10100 * kHz);
}

TEST_CASE("vfob copies a loaded vfoa freq and restores its mode", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 5, 10100 * kHz, 20000 * kHz);

    // Only vfoa_freq is seeded (in-band); vfob_* and vfoa_mode are absent.
    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfoa_freq", 14200 * kHz) == SUCCESS);

    SettingsManager mgr;
    fresh_manager(mgr, db, 5);

    REQUIRE(mgr.p_band_vfoa_freq.get() == 14200 * kHz);
    // vfoa_mode is restored from the loaded freq (14.2 MHz > 10 MHz -> USB).
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_usb);
    // vfob_freq copies the loaded vfoa_freq; vfob_mode copies vfoa_mode.
    REQUIRE(mgr.p_band_vfob_freq.get() == 14200 * kHz);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_usb);
}

TEST_CASE("VFO restore is quiet (no deferred write enqueued)", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 5, 10100 * kHz, 10150 * kHz);

    SettingsManager mgr;
    fresh_manager(mgr, db, 5);

    // Restored VFO values were applied with set_quiet: nothing is pending.
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::BAND, 5, "vfoa_freq"}).has_value() == false);
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::BAND, 5, "vfob_freq"}).has_value() == false);
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::BAND, 5, "vfoa_mode"}).has_value() == false);
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::BAND, 5, "vfob_mode"}).has_value() == false);
}

TEST_CASE("VFO restore default on undefined band", "[manager]") {
    TestDbGuard db;
    // No bands row at all: get_by_id fails -> vfoa_freq falls back to the
    // 12000000 default and the mode is derived from it (USB, >= 10 MHz).
    SettingsManager mgr;
    fresh_manager(mgr, db, 7);

    REQUIRE(mgr.p_band_vfoa_freq.get() == 12000 * kHz);
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_usb);
    REQUIRE(mgr.p_band_vfob_freq.get() == 12000 * kHz);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_usb);
}

TEST_CASE("implicit band switch keeps active VFO, restores inactive", "[manager]") {
    TestDbGuard db;
    // Band 5 is defined/empty (VFOs absent). Active VFO = A (0).
    insert_band(db.db, 5, 10100 * kHz, 20000 * kHz);
    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);

    // Tune the active VFO A, then cross into an implicitly-new empty band 6.
    mgr.p_band_vfoa_freq.set(10200 * kHz);
    insert_band(db.db, 6, 1400 * kHz, 2000 * kHz);
    mgr.switch_band_implicit(6);

    // Active VFO A is kept (not restored), B is restored.
    REQUIRE(mgr.p_band_vfoa_freq.get() == 10200 * kHz);
    REQUIRE(mgr.p_band_vfob_freq.get() == 10200 * kHz);
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_usb);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_usb);
}

TEST_CASE("explicit band switch restores both VFOs", "[manager]") {
    TestDbGuard db;
    insert_band(db.db, 5, 10100 * kHz, 20000 * kHz);
    insert_band(db.db, 6, 1400 * kHz, 2000 * kHz);
    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);

    SettingsManager mgr;
    mgr.init_load(5, 3);
    mgr.p_band_vfoa_freq.set(10200 * kHz);
    mgr.switch_band_explicit(6);

    // Explicit switch restores both VFOs from the low band 6 definition.
    REQUIRE(mgr.p_band_vfoa_freq.get() == 1400 * kHz);
    REQUIRE(mgr.p_band_vfoa_mode.get() == x6100_mode_lsb);
    REQUIRE(mgr.p_band_vfob_freq.get() == 1400 * kHz);
    REQUIRE(mgr.p_band_vfob_mode.get() == x6100_mode_lsb);
    // current_vfo is never touched by a switch.
    REQUIRE(mgr.p_band_current_vfo.get() == 0);
}

TEST_CASE("flush thread lifecycle starts and stops cleanly", "[manager][threads]") {
    TestDbGuard db;

    SettingsManager mgr;
    mgr.init_load(5, 3);

    mgr.start_flush_thread();
    REQUIRE(mgr.current_band_id() == 5);

    // Starting twice is a no-op (does not spawn a second thread).
    mgr.start_flush_thread();

    mgr.stop_flush_thread();
    REQUIRE(mgr.current_band_id() == 5);

    // The destructor also calls stop_flush_thread(); joining an already-stopped
    // thread is safe.
    mgr.stop_flush_thread();
}

TEST_CASE("flush thread persists pending writes within the wake timeout", "[manager][threads]") {
    TestDbGuard db;

    SettingsManager mgr;
    mgr.init_load(5, 3);

    // Queue a write (not yet in the DB).
    mgr.p_volume.set(80);
    REQUIRE(storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") != 80);

    mgr.start_flush_thread();

    // Poll the DB for up to ~5 s (the thread wakes on a 3 s timeout).
    bool persisted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (storage_policy_for(StorageType::GLOBAL).load_int(0, "volume") == 80) {
            persisted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(persisted);
    mgr.stop_flush_thread();
}

TEST_CASE("parameter validators clamp out-of-range values on set", "[manager]") {
    TestDbGuard db;

    SettingsManager mgr;
    mgr.init_load(5, 3);

    // Clamping validators bound the documented ranges.
    mgr.p_volume.set(500);
    REQUIRE(mgr.p_volume.get() == 100);

    mgr.p_squelch.set(-50);
    REQUIRE(mgr.p_squelch.get() == 0);

    mgr.p_mode_squelch.set(1000);
    REQUIRE(mgr.p_mode_squelch.get() == 100);

    // Enum-like params are intentionally unvalidated (values pass through).
    mgr.p_band_current_vfo.set(7);
    REQUIRE(mgr.p_band_current_vfo.get() == 7);
    mgr.p_mode_agc.set(9);
    REQUIRE(mgr.p_mode_agc.get() == 9);
}

TEST_CASE("set to an already-clamped value does not enqueue a write", "[manager]") {
    TestDbGuard db;

    SettingsManager mgr;
    mgr.init_load(5, 3);

    // 500 clamps to 100; pending write holds 100.
    mgr.p_volume.set(500);
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::GLOBAL, 0, "volume"}).value() == 100);

    // Setting 100 again is already the clamped value -> no change -> no write.
    mgr.p_volume.set(100);
    REQUIRE(PendingWritesTestAccess::peek_int(mgr.pending_writes_,
            StorageKey{StorageType::GLOBAL, 0, "volume"}).value() == 100);
}

TEST_CASE("encoder_bind round-trip via DB", "[manager]") {
    TestDbGuard db;
    SettingsManager mgr;
    mgr.init_load(5, 3);

    auto def = mgr.p_encoder_bind.get();
    REQUIRE(def.size() == static_cast<size_t>(EB_CTRL_FAST_ACCESS_LAST));
    REQUIRE(def[EB_CTRL_VOL]  == EB_BIND_VOL);
    REQUIRE(def[EB_CTRL_HMIC] == EB_BIND_VOL);
    REQUIRE(def[EB_CTRL_DNF]  == EB_BIND_MFK);
    REQUIRE(def[EB_CTRL_SQL]  == EB_BIND_NONE);

    std::string custom(EB_CTRL_FAST_ACCESS_LAST, EB_BIND_NONE);
    custom[EB_CTRL_FILTER_LOW] = EB_BIND_MFK;
    mgr.p_encoder_bind.set(custom);
    mgr.flush_all();

    SettingsManager mgr2;
    mgr2.init_load(5, 3);
    REQUIRE(mgr2.p_encoder_bind.get() == custom);
}

TEST_CASE("encoder_bind validator pads/truncates to FAST_ACCESS_LAST", "[manager]") {
    TestDbGuard db;
    SettingsManager mgr;
    mgr.init_load(5, 3);

    mgr.p_encoder_bind.set("VV");
    REQUIRE(mgr.p_encoder_bind.get().size() == static_cast<size_t>(EB_CTRL_FAST_ACCESS_LAST));
    REQUIRE(mgr.p_encoder_bind.get()[0] == 'V');
    REQUIRE(mgr.p_encoder_bind.get()[2] == EB_BIND_NONE);

    std::string long_str(100, 'X');
    mgr.p_encoder_bind.set(long_str);
    REQUIRE(mgr.p_encoder_bind.get().size() == static_cast<size_t>(EB_CTRL_FAST_ACCESS_LAST));
}

TEST_CASE("encoder_bind deferred write enqueued on change", "[manager]") {
    TestDbGuard db;
    SettingsManager mgr;
    mgr.init_load(5, 3);

    std::string val(EB_CTRL_FAST_ACCESS_LAST, EB_BIND_NONE);
    val[EB_CTRL_VOL] = EB_BIND_VOL;
    mgr.p_encoder_bind.set(val);

    auto stored = PendingWritesTestAccess::peek_text(mgr.pending_writes_,
        StorageKey{StorageType::GLOBAL, 0, "encoder_bind"});
    REQUIRE(stored.has_value());
    REQUIRE(*stored == val);
}

// Helpers for the mode-filter tests below.
namespace {

// Initialise a fresh manager whose active VFO mode == `mode` and whose MODE
// context is `mode`, with filter_low/filter_high seeded for that mode. This
// mirrors production (cp_cur_mode == mode context) so filter_mode() picks the
// right category and reads the right MODE pair.
void init_filter_manager(SettingsManager& mgr, TestDbGuard& db, int mode, int low, int high)
{
    StoragePolicy& b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", mode) == SUCCESS);
    StoragePolicy& m = storage_policy_for(StorageType::MODE);
    REQUIRE(m.save_int(mode, "filter_low", low) == SUCCESS);
    REQUIRE(m.save_int(mode, "filter_high", high) == SUCCESS);
    mgr.init_load(5, mode);
}

} // namespace

TEST_CASE("freq_step/spectrum_factor default and clamp", "[manager]") {
    TestDbGuard db;
    SettingsManager mgr;
    mgr.init_load(5, 3);

    // Test for default values
    REQUIRE(mgr.p_mode_freq_step.get() == 500);
    REQUIRE(mgr.p_mode_spectrum_factor.get() == 1);

    mgr.p_mode_freq_step.set(20000);
    REQUIRE(mgr.p_mode_freq_step.get() == 10000);
    mgr.p_mode_freq_step.set(0);
    REQUIRE(mgr.p_mode_freq_step.get() == 1);

    mgr.p_mode_spectrum_factor.set(100);
    REQUIRE(mgr.p_mode_spectrum_factor.get() == 8);
    mgr.p_mode_spectrum_factor.set(0);
    REQUIRE(mgr.p_mode_spectrum_factor.get() == 1);
}

TEST_CASE("cur_filter_* per category from DB-seeded filter pair", "[manager]") {
    TestDbGuard db;

    // SSB (usb): low/high mirror the stored pair directly.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_usb, 100, 3000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 100);
        REQUIRE(mgr.cp_cur_filter_high.get() == 3000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 2900);
    }
    // AM: low == 0, bw == filter_high.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_am, 500, 5000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_high.get() == 5000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 5000);
    }
    // FM (nfm): low == 0, bw == filter_high.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_nfm, 500, 6000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_high.get() == 6000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 6000);
    }
    // CW: centred on key_tone
    {
        SettingsManager mgr;
        // BW = 400, save low=0, high=bw
        init_filter_manager(mgr, db, x6100_mode_cw, 0, 400);
        auto key_tone = mgr.p_key_tone.get();
        REQUIRE(mgr.cp_cur_filter_low.get() == key_tone - 200);
        REQUIRE(mgr.cp_cur_filter_high.get() == key_tone + 200);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 400);
    }
}

TEST_CASE("cp_cur_filter_low/high.set reverse into MODE filter params", "[manager]") {
    TestDbGuard db;

    // SSB: writes the matching filter param.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_usb, 200, 3000);
        mgr.cp_cur_filter_low.set(300);
        REQUIRE(mgr.cp_cur_filter_low.get() == 300);
        REQUIRE(mgr.cp_cur_filter_high.get() == 3000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 2700);

        mgr.cp_cur_filter_high.set(3300);
        REQUIRE(mgr.cp_cur_filter_high.get() == 3300);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 3000);
    }
    // AM: low is a no-op; high writes filter_high.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_am, 500, 3000);
        mgr.cp_cur_filter_low.set(200);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);   // unchanged (always 0)
        mgr.cp_cur_filter_high.set(2500);
        REQUIRE(mgr.cp_cur_filter_high.get() == 2500);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 2500);
    }
    // CW: low/high map back to filter_high = 2*(key_tone -/+ v).
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_cw, 100, 300);
        mgr.p_key_tone.set(600);
        mgr.cp_cur_filter_low.set(200);
        REQUIRE(mgr.cp_cur_filter_low.get() == 200);
        REQUIRE(mgr.cp_cur_filter_high.get() == 1000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 800);

        mgr.cp_cur_filter_high.set(700);
        REQUIRE(mgr.cp_cur_filter_high.get() == 700);
        REQUIRE(mgr.cp_cur_filter_low.get() == 500);

        mgr.p_key_tone.set(800);
        REQUIRE(mgr.cp_cur_filter_high.get() == 900);
        REQUIRE(mgr.cp_cur_filter_low.get() == 700);
    }
}

TEST_CASE("cp_cur_filter_bw.set reverse per category", "[manager]") {
    TestDbGuard db;

    // AM: filter_high == bw.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_am, 500, 3000);
        mgr.cp_cur_filter_bw.set(2500);
        REQUIRE(mgr.cp_cur_filter_high.get() == 2500);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 2500);
    }
    // SSB symmetric: edges recentred about (low+high)/2, width preserved.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_usb, 500, 3000);
        mgr.cp_cur_filter_bw.set(1000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 1250);   // 1750 - 500
        REQUIRE(mgr.cp_cur_filter_high.get() == 2250);  // 1750 + 500
        REQUIRE(mgr.cp_cur_filter_bw.get() == 1000);
    }
    // SSB low<0 fallback: low -> 0, high -> bw.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_usb, 2000, 3000);
        mgr.cp_cur_filter_bw.set(6000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_high.get() == 6000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 6000);
    }
    // CW: filter_high == bw (no symmetric low/high rewrite). The cur_* edges
    // are then centred on key_tone with offset filter_high/2.
    {
        SettingsManager mgr;
        init_filter_manager(mgr, db, x6100_mode_cw, 500, 3000);
        mgr.p_key_tone.set(700);
        mgr.cp_cur_filter_bw.set(2000);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_high.get() == 2000);
        REQUIRE(mgr.cp_cur_filter_bw.get() == 2000);

        mgr.p_key_tone.set(900);
        REQUIRE(mgr.cp_cur_filter_low.get() == 0);
        REQUIRE(mgr.cp_cur_filter_high.get() == 2000);

        mgr.p_key_tone.set(1100);
        REQUIRE(mgr.cp_cur_filter_low.get() == 100);
        REQUIRE(mgr.cp_cur_filter_high.get() == 2100);
    }
}

TEST_CASE("cur_filter set chain terminates and stays consistent", "[manager]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_filter_manager(mgr, db, x6100_mode_usb, 500, 3000);

    // Repeated bw sets (bounded width client) must not recurse/overflow and
    // must keep cur_* mutually consistent each time.
    int last_bw = -1;
    for (int bw = 100; bw <= 6000; bw += 977) {
        mgr.cp_cur_filter_bw.set(bw);
        REQUIRE(mgr.cp_cur_filter_bw.get() >= 10);
        REQUIRE(mgr.cp_cur_filter_high.get() - mgr.cp_cur_filter_low.get() > 0);
        last_bw = mgr.cp_cur_filter_bw.get();
    }
    REQUIRE(last_bw > 0);
    REQUIRE(mgr.cp_cur_filter_high.get() == mgr.cp_cur_filter_low.get() + last_bw);
}

TEST_CASE("switch_mode recomputes cur_filter_* for the new mode's pair", "[manager]") {
    TestDbGuard db;

    // Seed both the SSB (usb) and CW mode pairs, plus the VFO mode.
    {
        StoragePolicy& b = storage_policy_for(StorageType::BAND);
        REQUIRE(b.save_int(5, "vfo", 0) == SUCCESS);
        REQUIRE(b.save_int(5, "vfoa_mode", x6100_mode_usb) == SUCCESS);
        StoragePolicy& m = storage_policy_for(StorageType::MODE);
        REQUIRE(m.save_int(x6100_mode_usb, "filter_low", 500) == SUCCESS);
        REQUIRE(m.save_int(x6100_mode_usb, "filter_high", 3000) == SUCCESS);
        REQUIRE(m.save_int(x6100_mode_cw, "filter_low", 0) == SUCCESS);
        REQUIRE(m.save_int(x6100_mode_cw, "filter_high", 200) == SUCCESS);
    }

    SettingsManager mgr;
    mgr.init_load(5, x6100_mode_usb);
    REQUIRE(mgr.cp_cur_filter_low.get() == 500);
    REQUIRE(mgr.cp_cur_filter_high.get() == 3000);

    // Switch to CW via cp_cur_mode: writes the VFO mode, triggers switch_mode(),
    // loads the CW filter pair, recomputes cur_* for the CW category.
    mgr.cp_cur_mode.set(x6100_mode_cw);
    mgr.p_key_tone.set(700);

    REQUIRE(mgr.current_mode_id() == x6100_mode_cw);
    REQUIRE(mgr.cp_cur_filter_low.get() == 600);   // key_tone - high/2
    REQUIRE(mgr.cp_cur_filter_high.get() == 800);  // key_tone + high/2
    REQUIRE(mgr.cp_cur_filter_bw.get() == 200);
}
