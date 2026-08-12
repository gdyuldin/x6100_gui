// test_atu_cache.cpp
// Tests for the ATU migration to cfg: AtuTable (raw DB access on the
// legacy `atu` table) and AtuNetworkCache (reactive nearest-network cache).
// Runs against an in-memory SQLite connection with the same atu schema as
// sql/params.sql.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <vector>

#include "atu_cache.h"
#include "db.h"

namespace {

// RAII wrapper for the in-memory DB. Creates the atu table with the same
// schema as sql/params.sql and initialises the AtuTable prepared statements.
struct TestAtuDbGuard {
    sqlite3 *db = nullptr;

    TestAtuDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char *err = nullptr;
        int   rc  = sqlite3_exec(db,
                                 "CREATE TABLE IF NOT EXISTS atu("
                                    "  ant     INTEGER,"
                                    "  freq    INTEGER,"
                                    "  val     INTEGER,"
                                    "  UNIQUE (ant, freq) ON CONFLICT REPLACE"
                                    ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
        REQUIRE(AtuTable::Init(db));
    }

    ~TestAtuDbGuard() {
        AtuTable::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

TEST_CASE("AtuTable round-trip save/load returns entries in freq order", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 5) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 30000, 9) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 20000, 7) == SUCCESS);

    std::vector<AtuTable::AtuEntry> out;
    REQUIRE(AtuTable::LoadAll(1, out) == SUCCESS);
    REQUIRE(out.size() == 3);
    REQUIRE(out[0].freq == 10000);
    REQUIRE(out[0].network == 5);
    REQUIRE(out[1].freq == 20000);
    REQUIRE(out[1].network == 7);
    REQUIRE(out[2].freq == 30000);
    REQUIRE(out[2].network == 9);
}

TEST_CASE("AtuTable save overwrites the same (ant, freq) row", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 5) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 10000, 8) == SUCCESS);

    std::vector<AtuTable::AtuEntry> out;
    REQUIRE(AtuTable::LoadAll(1, out) == SUCCESS);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].freq == 10000);
    REQUIRE(out[0].network == 8);
}

TEST_CASE("AtuTable DeleteAdjacent prunes in-range rows but keeps out-of-range", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 5) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 20000, 7) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 30000, 9) == SUCCESS);

    // Range [25000 - 10000, 25000 + 10000] = [15000, 35000] excludes 10000.
    REQUIRE(AtuTable::DeleteAdjacent(1, 25000, 10000) == SUCCESS);

    std::vector<AtuTable::AtuEntry> out;
    REQUIRE(AtuTable::LoadAll(1, out) == SUCCESS);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].freq == 10000);
}

TEST_CASE("AtuTable DeleteAdjacent preserves the exact match", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 25000, 7) == SUCCESS);

    // Range [0, 50000]; the just-saved row is excluded by :freq != freq.
    REQUIRE(AtuTable::DeleteAdjacent(1, 25000, 25000) == SUCCESS);

    std::vector<AtuTable::AtuEntry> out;
    REQUIRE(AtuTable::LoadAll(1, out) == SUCCESS);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].freq == 25000);
    REQUIRE(out[0].network == 7);
}

TEST_CASE("AtuNetworkCache finds the nearest network within the save step", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 11) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 20000, 22) == SUCCESS);
    REQUIRE(AtuTable::Save(1, 35000, 33) == SUCCESS);

    AtuNetworkCache cache;

    cache.on_params_changed(1, 15000, true);
    REQUIRE(cache.loaded.get() == true);
    REQUIRE(cache.network.get() == 11);

    cache.on_params_changed(1, 18000, true);
    REQUIRE(cache.loaded.get() == true);
    REQUIRE(cache.network.get() == 22);

    cache.on_params_changed(1, 40000, true);
    REQUIRE(cache.loaded.get() == true);
    REQUIRE(cache.network.get() == 33);

    // 70000 is more than 25 kHz from the nearest entry (35000).
    cache.on_params_changed(1, 70000, true);
    REQUIRE(cache.loaded.get() == false);
    REQUIRE(cache.network.get() == 0);
}

TEST_CASE("AtuNetworkCache with atu_enabled false clears subjects", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 11) == SUCCESS);

    AtuNetworkCache cache;
    cache.on_params_changed(1, 10000, true);
    REQUIRE(cache.loaded.get() == true);
    REQUIRE(cache.network.get() == 11);

    // Toggle off: the legacy code returned early and kept the last values;
    // the new cache intentionally clears them.
    cache.on_params_changed(1, 10000, false);
    REQUIRE(cache.loaded.get() == false);
    REQUIRE(cache.network.get() == 0);
}

TEST_CASE("AtuNetworkCache save_network persists, prunes adjacent and publishes", "[atu]") {
    TestAtuDbGuard db;

    // Pre-existing adjacent entry that must be pruned by save_network.
    REQUIRE(AtuTable::Save(1, 10000, 5) == SUCCESS);

    AtuNetworkCache cache;
    REQUIRE(cache.save_network(1, 20000, 77) == SUCCESS);

    REQUIRE(cache.loaded.get() == true);
    REQUIRE(cache.network.get() == 77);

    std::vector<AtuTable::AtuEntry> out;
    REQUIRE(AtuTable::LoadAll(1, out) == SUCCESS);
    REQUIRE(out.size() == 1); // 10000 is within ±25 kHz of 20000 and pruned
    REQUIRE(out[0].freq == 20000);
    REQUIRE(out[0].network == 77);
}

TEST_CASE("AtuNetworkCache antenna isolation keeps caches separate", "[atu]") {
    TestAtuDbGuard db;

    REQUIRE(AtuTable::Save(1, 10000, 11) == SUCCESS);

    AtuNetworkCache cache;
    cache.on_params_changed(1, 12000, true);
    REQUIRE(cache.loaded.get() == true);

    // Switch to antenna 2, whose cache is empty.
    cache.on_params_changed(2, 12000, true);
    REQUIRE(cache.loaded.get() == false);
    REQUIRE(cache.network.get() == 0);
}
