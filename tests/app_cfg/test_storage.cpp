// test_storage.cpp
// Tests for StoragePolicy (GlobalStorage / BandStorage / ModeStorage):
// round-trip save/load against an in-memory SQLite connection, per-value-type
// correctness (int32/float/string) and context_id isolation for band/mode.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <sqlite3.h>

#include "db.h"
#include "parameter.h"
#include "storage_policy.h"

namespace {

// RAII wrapper for the in-memory DB. Creates all three params tables with the
// same schema as sql/params.sql and initialises the shared prepared statements
// of every table class used by the storage policies.
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
            ");", nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err) sqlite3_free(err);
        ParamsTable::Init(db);
        BandParamsTable::Init(db);
        ModeParamsTable::Init(db);
    }

    ~TestDbGuard() {
        ParamsTable::Shutdown();
        BandParamsTable::Shutdown();
        ModeParamsTable::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

TEST_CASE("GlobalStorage round-trip int32/float/string", "[storage]") {
    TestDbGuard db;
    StoragePolicy& policy = storage_policy_for(StorageType::GLOBAL);

    REQUIRE(policy.save_int(0, "volume", 75) == SUCCESS);
    auto v_int = policy.load_int(0, "volume");
    REQUIRE(v_int.has_value());
    REQUIRE(*v_int == 75);

    REQUIRE(policy.save_float(0, "squelch", 3.5f) == SUCCESS);
    auto v_float = policy.load_float(0, "squelch");
    REQUIRE(v_float.has_value());
    REQUIRE(*v_float == Catch::Approx(3.5f));

    REQUIRE(policy.save_text(0, "callsign", "R2ABC") == SUCCESS);
    auto v_text = policy.load_text(0, "callsign");
    REQUIRE(v_text.has_value());
    REQUIRE(*v_text == "R2ABC");
}

TEST_CASE("GlobalStorage load of missing key returns nullopt", "[storage]") {
    TestDbGuard db;
    StoragePolicy& policy = storage_policy_for(StorageType::GLOBAL);

    REQUIRE_FALSE(policy.load_int(0, "no_such_param").has_value());
    REQUIRE_FALSE(policy.load_float(0, "no_such_param").has_value());
    REQUIRE_FALSE(policy.load_text(0, "no_such_param").has_value());
}

TEST_CASE("BandStorage round-trip isolates context_id", "[storage]") {
    TestDbGuard db;
    StoragePolicy& policy = storage_policy_for(StorageType::BAND);

    REQUIRE(policy.save_int(5, "vfoa_freq", 14'200'000) == SUCCESS);
    REQUIRE(policy.save_int(6, "vfoa_freq", 14'300'000) == SUCCESS);

    auto v5 = policy.load_int(5, "vfoa_freq");
    auto v6 = policy.load_int(6, "vfoa_freq");
    REQUIRE(v5.has_value());
    REQUIRE(v6.has_value());
    REQUIRE(*v5 == 14'200'000);
    REQUIRE(*v6 == 14'300'000);

    // Missing name in an existing context still returns nullopt.
    REQUIRE_FALSE(policy.load_int(5, "no_such_param").has_value());
}

TEST_CASE("BandStorage round-trip float and string", "[storage]") {
    TestDbGuard db;
    StoragePolicy& policy = storage_policy_for(StorageType::BAND);

    REQUIRE(policy.save_float(5, "tone", 600.0f) == SUCCESS);
    auto v_float = policy.load_float(5, "tone");
    REQUIRE(v_float.has_value());
    REQUIRE(*v_float == Catch::Approx(600.0f));

    REQUIRE(policy.save_text(5, "label", "40m") == SUCCESS);
    auto v_text = policy.load_text(5, "label");
    REQUIRE(v_text.has_value());
    REQUIRE(*v_text == "40m");
}

TEST_CASE("ModeStorage round-trip with mode context_id", "[storage]") {
    TestDbGuard db;
    StoragePolicy& policy = storage_policy_for(StorageType::MODE);

    REQUIRE(policy.save_int(3, "squelch", 12) == SUCCESS);
    auto v = policy.load_int(3, "squelch");
    REQUIRE(v.has_value());
    REQUIRE(*v == 12);

    // A different mode does not see the value.
    REQUIRE_FALSE(policy.load_int(4, "squelch").has_value());

    REQUIRE(policy.save_float(3, "tone", 700.0f) == SUCCESS);
    auto vf = policy.load_float(3, "tone");
    REQUIRE(vf.has_value());
    REQUIRE(*vf == Catch::Approx(700.0f));

    REQUIRE(policy.save_text(3, "label", "USB") == SUCCESS);
    auto vt = policy.load_text(3, "label");
    REQUIRE(vt.has_value());
    REQUIRE(*vt == "USB");
}
