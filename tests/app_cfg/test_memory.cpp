// test_memory.cpp
// Tests for MemoryTable (user memory slots on the `memory` table): round-trip
// save/load, missing-slot handling, partial loads, single-field overwrite and
// slot isolation. Runs against an in-memory SQLite connection.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include "db.h"

namespace {

// RAII wrapper for the in-memory DB. Creates the memory table with the same
// schema as sql/params.sql and initialises the MemoryTable prepared statements.
struct TestDbGuard {
    sqlite3 *db = nullptr;

    TestDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char *err = nullptr;
        int   rc  = sqlite3_exec(db,
                                 "CREATE TABLE IF NOT EXISTS memory("
                                    "  id     INTEGER,"
                                    "  name   TEXT,"
                                    "  val    INTEGER,"
                                    "  UNIQUE (id, name) ON CONFLICT REPLACE"
                                    ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
        REQUIRE(MemoryTable::Init(db));
    }

    ~TestDbGuard() {
        MemoryTable::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

TEST_CASE("MemoryTable round-trip save/load", "[memory]") {
    TestDbGuard db;

    REQUIRE(MemoryTable::Save(1, "vfoa_freq", 14200000) == SUCCESS);
    REQUIRE(MemoryTable::Save(1, "vfoa_mode", 3) == SUCCESS);
    REQUIRE(MemoryTable::Save(1, "vfoa_agc", 4) == SUCCESS);
    REQUIRE(MemoryTable::Save(1, "vfoa_pre", 1) == SUCCESS);
    REQUIRE(MemoryTable::Save(1, "vfoa_att", 0) == SUCCESS);

    int32_t freq, mode, agc, att, pre;
    bool    has_freq, has_mode, has_agc, has_att, has_pre;
    REQUIRE(MemoryTable::Load(1, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));

    REQUIRE(has_freq);
    REQUIRE(freq == 14200000);
    REQUIRE(has_mode);
    REQUIRE(mode == 3);
    REQUIRE(has_agc);
    REQUIRE(agc == 4);
    REQUIRE(has_pre);
    REQUIRE(pre == 1);
    REQUIRE(has_att);
    REQUIRE(att == 0);
}

TEST_CASE("MemoryTable load of missing slot returns false", "[memory]") {
    TestDbGuard db;

    int32_t freq, mode, agc, att, pre;
    bool    has_freq, has_mode, has_agc, has_att, has_pre;
    REQUIRE_FALSE(MemoryTable::Load(99, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));
}

TEST_CASE("MemoryTable partial load reports only present fields", "[memory]") {
    TestDbGuard db;

    // Slot without a vfoa_freq row is not loadable.
    REQUIRE(MemoryTable::Save(2, "vfoa_mode", 3) == SUCCESS);

    int32_t freq, mode, agc, att, pre;
    bool    has_freq, has_mode, has_agc, has_att, has_pre;
    REQUIRE_FALSE(MemoryTable::Load(2, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));

    // Add the freq; only freq+mode are present, the rest must be flagged as absent.
    REQUIRE(MemoryTable::Save(2, "vfoa_freq", 14074000) == SUCCESS);
    REQUIRE(MemoryTable::Load(2, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));
    REQUIRE(has_freq);
    REQUIRE(freq == 14074000);
    REQUIRE(has_mode);
    REQUIRE(mode == 3);
    REQUIRE_FALSE(has_agc);
    REQUIRE_FALSE(has_pre);
    REQUIRE_FALSE(has_att);
}

TEST_CASE("MemoryTable overwrite of a single field keeps the latest value", "[memory]") {
    TestDbGuard db;

    REQUIRE(MemoryTable::Save(3, "vfoa_freq", 7000000) == SUCCESS);
    REQUIRE(MemoryTable::Save(3, "vfoa_freq", 7050000) == SUCCESS);

    int32_t freq, mode, agc, att, pre;
    bool    has_freq, has_mode, has_agc, has_att, has_pre;
    REQUIRE(MemoryTable::Load(3, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));
    REQUIRE(has_freq);
    REQUIRE(freq == 7050000);
}

TEST_CASE("MemoryTable slots are isolated by id", "[memory]") {
    TestDbGuard db;

    REQUIRE(MemoryTable::Save(1, "vfoa_freq", 14200000) == SUCCESS);
    REQUIRE(MemoryTable::Save(2, "vfoa_freq", 3573000) == SUCCESS);

    int32_t freq, mode, agc, att, pre;
    bool    has_freq, has_mode, has_agc, has_att, has_pre;

    REQUIRE(MemoryTable::Load(1, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));
    REQUIRE(has_freq);
    REQUIRE(freq == 14200000);

    REQUIRE(MemoryTable::Load(2, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre));
    REQUIRE(has_freq);
    REQUIRE(freq == 3573000);
}
