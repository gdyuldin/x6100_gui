// test_db.cpp
// Tests for ParamsTable and ModeParamsTable: Save/Load round-trips for
// int32_t, float and std::string against in-memory SQLite databases. Only the
// tables needed by the tested class are created, and each table is
// initialised directly so the tests never touch cfg_db_init()/exit().
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sqlite3.h>

#include <string>

#include "db.h"


// RAII fixture: opens an in-memory database, creates the params table and
// initialises ParamsTable. Shuts ParamsTable down and closes the connection
// on destruction.
class ParamsTableFixture {
  public:
    ParamsTableFixture() {
        // Validate the fixture prerequisites explicitly.
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char* create_sql =
            "CREATE TABLE params("
            "    name TEXT PRIMARY KEY ON CONFLICT REPLACE,"
            "    val  INTEGER"
            ");";
        rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = ParamsTable::Init(db_);
        REQUIRE(ok);
    }

    ~ParamsTableFixture() {
        ParamsTable::Shutdown();
        sqlite3_close(db_);
    }

    sqlite3* db() {
        return db_;
    }

  private:
    sqlite3* db_ = nullptr;
};


// RAII fixture: opens an in-memory database, creates the mode_params table
// and initialises ModeParamsTable. Shuts ModeParamsTable down and closes the
// connection on destruction.
class ModeParamsTableFixture {
  public:
    ModeParamsTableFixture() {
        // Validate the fixture prerequisites explicitly.
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char* create_sql =
            "CREATE TABLE mode_params("
            "    mode  INTEGER,"
            "    name  TEXT,"
            "    val   INTEGER,"
            "    UNIQUE(mode, name) ON CONFLICT REPLACE"
            ");";
        rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = ModeParamsTable::Init(db_);
        REQUIRE(ok);
    }

    ~ModeParamsTableFixture() {
        ModeParamsTable::Shutdown();
        sqlite3_close(db_);
    }

  private:
    sqlite3* db_ = nullptr;
};


// ---------------------------------------------------------------------------
// ParamsTable
// ---------------------------------------------------------------------------

TEST_CASE("ParamsTable saves and loads int32_t", "[db]") {
    ParamsTableFixture f;

    int rc = ParamsTable::Save<int32_t>("vol", 42);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<int32_t> res = ParamsTable::Load<int32_t>("vol");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == 42);
}

TEST_CASE("ParamsTable saves and loads float", "[db]") {
    ParamsTableFixture f;

    int rc = ParamsTable::Save<float>("pwr", 14.175f);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<float> res = ParamsTable::Load<float>("pwr");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == Catch::Approx(14.175f));
}

TEST_CASE("ParamsTable saves and loads std::string", "[db]") {
    ParamsTableFixture f;

    int rc = ParamsTable::Save<std::string>("callsign", "R1XXX");
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<std::string> res = ParamsTable::Load<std::string>("callsign");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == "R1XXX");
}

TEST_CASE("ParamsTable type switch under the same key", "[db]") {
    ParamsTableFixture f;

    // Write an int, then overwrite it as float and as text.
    REQUIRE(ParamsTable::Save<int32_t>("mixed", 1000) == SUCCESS);
    REQUIRE(ParamsTable::Load<int32_t>("mixed").value == 1000);

    REQUIRE(ParamsTable::Save<float>("mixed", 3.5f) == SUCCESS);
    REQUIRE(ParamsTable::Load<float>("mixed").value == Catch::Approx(3.5f));

    REQUIRE(ParamsTable::Save<std::string>("mixed", "txt") == SUCCESS);
    REQUIRE(ParamsTable::Load<std::string>("mixed").value == "txt");

    // Overwrite text with an int again.
    REQUIRE(ParamsTable::Save<int32_t>("mixed", 7) == SUCCESS);
    REQUIRE(ParamsTable::Load<int32_t>("mixed").value == 7);
}

TEST_CASE("ParamsTable returns NOT_FOUND for a missing key", "[db]") {
    ParamsTableFixture f;

    ParamLoadResult<int32_t> res = ParamsTable::Load<int32_t>("missing");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("ParamsTable handles an empty string value", "[db]") {
    ParamsTableFixture f;

    REQUIRE(ParamsTable::Save<std::string>("empty", "") == SUCCESS);

    ParamLoadResult<std::string> res = ParamsTable::Load<std::string>("empty");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.empty());
}

TEST_CASE("ParamsTable supports re-initialisation on the same connection", "[db]") {
    ParamsTableFixture f;

    // First save/load cycle.
    REQUIRE(ParamsTable::Save<int32_t>("reinit", 11) == SUCCESS);
    REQUIRE(ParamsTable::Load<int32_t>("reinit").value == 11);

    // Shut down and re-initialise the table on the same connection. The
    // statements are re-prepared; previously stored data must stay intact.
    ParamsTable::Shutdown();
    bool ok = ParamsTable::Init(f.db());
    REQUIRE(ok);

    REQUIRE(ParamsTable::Load<int32_t>("reinit").value == 11);
    REQUIRE(ParamsTable::Save<std::string>("reinit", "again") == SUCCESS);
    REQUIRE(ParamsTable::Load<std::string>("reinit").value == "again");
}


// ---------------------------------------------------------------------------
// ModeParamsTable
// ---------------------------------------------------------------------------

TEST_CASE("ModeParamsTable saves and loads int32_t", "[db]") {
    ModeParamsTableFixture f;

    int rc = ModeParamsTable::Save<int32_t>(3, "agc", 5);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<int32_t> res = ModeParamsTable::Load<int32_t>(3, "agc");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == 5);
}

TEST_CASE("ModeParamsTable saves and loads float", "[db]") {
    ModeParamsTableFixture f;

    int rc = ModeParamsTable::Save<float>(7, "cutoff", 2.4f);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<float> res = ModeParamsTable::Load<float>(7, "cutoff");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == Catch::Approx(2.4f));
}

TEST_CASE("ModeParamsTable saves and loads std::string", "[db]") {
    ModeParamsTableFixture f;

    int rc = ModeParamsTable::Save<std::string>(2, "label", "LSB");
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<std::string> res = ModeParamsTable::Load<std::string>(2, "label");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == "LSB");
}

TEST_CASE("ModeParamsTable overwrites a value under the same (mode, name) key", "[db]") {
    ModeParamsTableFixture f;

    REQUIRE(ModeParamsTable::Save<int32_t>(3, "agc", 5) == SUCCESS);
    REQUIRE(ModeParamsTable::Load<int32_t>(3, "agc").value == 5);

    // INSERT OR REPLACE on (mode, name) must replace the stored value.
    REQUIRE(ModeParamsTable::Save<int32_t>(3, "agc", 9) == SUCCESS);
    REQUIRE(ModeParamsTable::Load<int32_t>(3, "agc").value == 9);
}

TEST_CASE("ModeParamsTable isolates values between different modes", "[db]") {
    ModeParamsTableFixture f;

    REQUIRE(ModeParamsTable::Save<int32_t>(3, "agc", 5) == SUCCESS);
    REQUIRE(ModeParamsTable::Save<int32_t>(7, "agc", 8) == SUCCESS);

    REQUIRE(ModeParamsTable::Load<int32_t>(3, "agc").value == 5);
    REQUIRE(ModeParamsTable::Load<int32_t>(7, "agc").value == 8);
}

TEST_CASE("ModeParamsTable returns NOT_FOUND for a missing key", "[db]") {
    ModeParamsTableFixture f;

    ParamLoadResult<int32_t> res = ModeParamsTable::Load<int32_t>(3, "missing");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("ModeParamsTable returns NOT_FOUND when the mode has no entry", "[db]") {
    ModeParamsTableFixture f;

    REQUIRE(ModeParamsTable::Save<int32_t>(3, "agc", 5) == SUCCESS);

    // The (mode, name) pair for mode 9 was never written.
    ParamLoadResult<int32_t> res = ModeParamsTable::Load<int32_t>(9, "agc");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("ModeParamsTable handles an empty string value", "[db]") {
    ModeParamsTableFixture f;

    REQUIRE(ModeParamsTable::Save<std::string>(4, "empty", "") == SUCCESS);

    ParamLoadResult<std::string> res = ModeParamsTable::Load<std::string>(4, "empty");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.empty());
}


// ---------------------------------------------------------------------------
// BandParamsTable
// ---------------------------------------------------------------------------

// RAII fixture: opens an in-memory database, creates the band_params table
// and initialises BandParamsTable. Shuts BandParamsTable down and closes the
// connection on destruction.
class BandParamsTableFixture {
  public:
    BandParamsTableFixture() {
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char* create_sql =
            "CREATE TABLE band_params("
            "    bands_id INTEGER,"
            "    name     TEXT,"
            "    val      INTEGER,"
            "    UNIQUE(bands_id, name) ON CONFLICT REPLACE"
            ");";
        rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = BandParamsTable::Init(db_);
        REQUIRE(ok);
    }

    ~BandParamsTableFixture() {
        BandParamsTable::Shutdown();
        sqlite3_close(db_);
    }

  private:
    sqlite3* db_ = nullptr;
};

TEST_CASE("BandParamsTable saves and loads int32_t", "[db]") {
    BandParamsTableFixture f;

    int rc = BandParamsTable::Save<int32_t>(1, "tx_gain", 50);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<int32_t> res = BandParamsTable::Load<int32_t>(1, "tx_gain");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == 50);
}

TEST_CASE("BandParamsTable saves and loads int32_t with BAND_UNDEFINED", "[db]") {
    BandParamsTableFixture f;

    int rc = BandParamsTable::Save<int32_t>(BAND_UNDEFINED, "tx_gain", 30);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<int32_t> res = BandParamsTable::Load<int32_t>(BAND_UNDEFINED, "tx_gain");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == 30);
}

TEST_CASE("BandParamsTable saves and loads float", "[db]") {
    BandParamsTableFixture f;

    int rc = BandParamsTable::Save<float>(2, "cutter", 2.5f);
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<float> res = BandParamsTable::Load<float>(2, "cutter");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == Catch::Approx(2.5f));
}

TEST_CASE("BandParamsTable saves and loads std::string", "[db]") {
    BandParamsTableFixture f;

    int rc = BandParamsTable::Save<std::string>(3, "label", "40m");
    REQUIRE(rc == SUCCESS);

    ParamLoadResult<std::string> res = BandParamsTable::Load<std::string>(3, "label");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value == "40m");
}

TEST_CASE("BandParamsTable overwrites a value under the same (band_id, name) key", "[db]") {
    BandParamsTableFixture f;

    REQUIRE(BandParamsTable::Save<int32_t>(1, "tx_gain", 50) == SUCCESS);
    REQUIRE(BandParamsTable::Load<int32_t>(1, "tx_gain").value == 50);

    // INSERT OR REPLACE on (bands_id, name) must replace the stored value.
    REQUIRE(BandParamsTable::Save<int32_t>(1, "tx_gain", 80) == SUCCESS);
    REQUIRE(BandParamsTable::Load<int32_t>(1, "tx_gain").value == 80);
}

TEST_CASE("BandParamsTable isolates values between different bands", "[db]") {
    BandParamsTableFixture f;

    REQUIRE(BandParamsTable::Save<int32_t>(1, "tx_gain", 50) == SUCCESS);
    REQUIRE(BandParamsTable::Save<int32_t>(2, "tx_gain", 70) == SUCCESS);

    REQUIRE(BandParamsTable::Load<int32_t>(1, "tx_gain").value == 50);
    REQUIRE(BandParamsTable::Load<int32_t>(2, "tx_gain").value == 70);
}

TEST_CASE("BandParamsTable returns NOT_FOUND for a missing key", "[db]") {
    BandParamsTableFixture f;

    ParamLoadResult<int32_t> res = BandParamsTable::Load<int32_t>(1, "missing");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("BandParamsTable handles an empty string value", "[db]") {
    BandParamsTableFixture f;

    REQUIRE(BandParamsTable::Save<std::string>(4, "empty", "") == SUCCESS);

    ParamLoadResult<std::string> res = BandParamsTable::Load<std::string>(4, "empty");
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.empty());
}


// ---------------------------------------------------------------------------
// BandsTable
// ---------------------------------------------------------------------------

// RAII fixture: opens an in-memory database, creates the bands table
// (matching the production schema from sql/bands_*.csv) and initialises
// BandsTable. Shuts BandsTable down and closes the connection on destruction.
//
// Two active bands are adjacent (`40m CW` stops exactly where `40m SSB`
// starts) to mirror the real band layout where neighbouring ranges share a
// boundary. One inactive band (type 0) is present as well. type values follow
// the real sql/bands_*.csv data: 0 (inactive) and 1 (active).
class BandsTableFixture {
  public:
    BandsTableFixture() {
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char* create_sql =
            "CREATE TABLE bands("
            "    id         INTEGER PRIMARY KEY,"
            "    name       TEXT,"
            "    start_freq INTEGER,"
            "    stop_freq  INTEGER,"
            "    type       INTEGER"
            ");";
        rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        const char* insert_sql =
            "INSERT INTO bands(id, name, start_freq, stop_freq, type) VALUES"
            "    (1, '80m',    3500000, 4000000, 1),"
            "    (2, '40m CW', 7000000, 7050000, 1),"
            "    (3, '40m SSB',7050000, 7200000, 1),"
            "    (4, 'GAP',    5000000, 6000000, 0);";
        rc = sqlite3_exec(db_, insert_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = BandsTable::Init(db_);
        REQUIRE(ok);
    }

    ~BandsTableFixture() {
        BandsTable::Shutdown();
        sqlite3_close(db_);
    }

    sqlite3* db() {
        return db_;
    }

  private:
    sqlite3* db_ = nullptr;
};

TEST_CASE("BandsTable get_by_id loads a band and its type", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_id(2);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 2);
    REQUIRE(res.value.name == "40m CW");
    REQUIRE(res.value.start_freq == 7'000'000);
    REQUIRE(res.value.stop_freq == 7'050'000);
    REQUIRE(res.value.type == BAND_ACTIVE);
}

TEST_CASE("BandsTable get_by_id loads a second adjacent active band", "[db]") {
    BandsTableFixture f;

    // The neighbour band starts exactly where "40m CW" stops.
    BandInfoLoadResult res = BandsTable::get_by_id(3);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 3);
    REQUIRE(res.value.name == "40m SSB");
    REQUIRE(res.value.start_freq == 7'050'000);
    REQUIRE(res.value.stop_freq == 7'200'000);
    REQUIRE(res.value.type == BAND_ACTIVE);
}

TEST_CASE("BandsTable get_by_id loads an inactive band", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_id(4);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 4);
    REQUIRE(res.value.name == "GAP");
    REQUIRE(res.value.type == BAND_INACTIVE);
}

TEST_CASE("BandsTable get_by_id returns NOT_FOUND for a missing id", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_id(99);
    REQUIRE(res.rc == NOT_FOUND);
    // The default BandInfo must have no side effects on later lookups.
    REQUIRE(res.value.id == BAND_UNDEFINED);
}

TEST_CASE("BandsTable get_by_id returns NOT_FOUND for BAND_UNDEFINED", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_id(BAND_UNDEFINED);
    REQUIRE(res.rc == NOT_FOUND);
    REQUIRE(res.value.id == BAND_UNDEFINED);
}

TEST_CASE("BandsTable get_by_id serves repeated lookups from cache", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult first = BandsTable::get_by_id(2);
    REQUIRE(first.rc == SUCCESS);
    REQUIRE(first.value.name == "40m CW");

    // Second lookup for the same id must return the cached band.
    BandInfoLoadResult second = BandsTable::get_by_id(2);
    REQUIRE(second.rc == SUCCESS);
    REQUIRE(second.value.id == 2);
    REQUIRE(second.value.name == "40m CW");

    // Cache is keyed by id: loading the neighbour must not return the cached
    // "40m CW" entry.
    BandInfoLoadResult neighbour = BandsTable::get_by_id(3);
    REQUIRE(neighbour.rc == SUCCESS);
    REQUIRE(neighbour.value.id == 3);
    REQUIRE(neighbour.value.name == "40m SSB");
}

TEST_CASE("BandsTable get_by_freq returns a gap below the first band", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_freq(1'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == BAND_UNDEFINED);
    REQUIRE(res.value.type == BAND_INACTIVE);
    // No active band below 1 MHz: the low side is NULL (start_freq becomes 0)
    // and the next band starts at 3.5 MHz.
    REQUIRE(res.value.start_freq == 0);
    REQUIRE(res.value.stop_freq == 3'500'000);
}

TEST_CASE("BandsTable get_by_freq loads the active band containing the frequency", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_freq(3'600'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 1);
    REQUIRE(res.value.name == "80m");
    REQUIRE(res.value.start_freq == 3'500'000);
    REQUIRE(res.value.stop_freq == 4'000'000);
    REQUIRE(res.value.type == BAND_ACTIVE);
}

TEST_CASE("BandsTable get_by_freq includes the lower band edge", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_freq(7'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 2);
    REQUIRE(res.value.name == "40m CW");
    REQUIRE(res.value.start_freq == 7'000'000);
    REQUIRE(res.value.stop_freq == 7'050'000);
}

TEST_CASE("BandsTable get_by_freq includes the upper band edge", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::get_by_freq(4'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 1);
    REQUIRE(res.value.name == "80m");
    REQUIRE(res.value.start_freq == 3'500'000);
    REQUIRE(res.value.stop_freq == 4'000'000);
}

TEST_CASE("BandsTable get_by_freq resolves overlapping adjacent bands by id", "[db]") {
    BandsTableFixture f;

    // 7.05 MHz belongs to both 40m CW (which stops here) and 40m SSB (which
    // starts here); ORDER BY id DESC NULLS LAST picks the band with the
    // highest id.
    BandInfoLoadResult res = BandsTable::get_by_freq(7'050'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == 3);
    REQUIRE(res.value.name == "40m SSB");
    REQUIRE(res.value.start_freq == 7'050'000);
    REQUIRE(res.value.stop_freq == 7'200'000);
}

TEST_CASE("BandsTable get_by_freq returns the gap between bands ignoring inactive bands", "[db]") {
    BandsTableFixture f;

    // 5 MHz lies inside the inactive "GAP" band (type = 0). Active bands are
    // 80m (3.5-4 MHz) below and 40m CW (7-7.05 MHz) above, so the gap spans
    // 4-7 MHz.
    BandInfoLoadResult res = BandsTable::get_by_freq(5'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == BAND_UNDEFINED);
    REQUIRE(res.value.type == BAND_INACTIVE);
    REQUIRE(res.value.start_freq == 4'000'000);
    REQUIRE(res.value.stop_freq == 7'000'000);
}

TEST_CASE("BandsTable get_by_freq returns a gap above the last band", "[db]") {
    BandsTableFixture f;

    // No active band above 7.2 MHz: the high side is NULL (stop_freq becomes
    // 0xFFFFFFFF) and the low side is the stop of 40m SSB.
    BandInfoLoadResult res = BandsTable::get_by_freq(8'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.id == BAND_UNDEFINED);
    REQUIRE(res.value.type == BAND_INACTIVE);
    REQUIRE(res.value.start_freq == 7'200'000);
    REQUIRE(res.value.stop_freq == 4294967295U);
}

TEST_CASE("BandsTable get_by_freq serves repeated lookups from cache", "[db]") {    BandsTableFixture f;

    // Load 7.1 MHz -> 40m SSB is now cached.
    BandInfoLoadResult first = BandsTable::get_by_freq(7'100'000);
    REQUIRE(first.rc == SUCCESS);
    REQUIRE(first.value.id == 3);
    REQUIRE(first.value.name == "40m SSB");

    // Shrink the band in the DB; the cached copy must still be served.
    int rc = sqlite3_exec(f.db(), "UPDATE bands SET stop_freq = 7100000 WHERE id = 3", nullptr, nullptr, nullptr);
    REQUIRE(rc == SQLITE_OK);

    BandInfoLoadResult second = BandsTable::get_by_freq(7'100'000);
    REQUIRE(second.rc == SUCCESS);
    REQUIRE(second.value.id == 3);
    REQUIRE(second.value.name == "40m SSB");
    REQUIRE(second.value.stop_freq == 7'200'000);
}

TEST_CASE("BandsTable get_by_freq serves the start boundary from cache", "[db]") {
    BandsTableFixture f;

    // 7.0 MHz is exactly the start of 40m CW. The cache must serve this
    // boundary on the next call (the old code compared with strict > and
    // missed, forcing a re-query).
    BandInfoLoadResult first = BandsTable::get_by_freq(7'000'000);
    REQUIRE(first.rc == SUCCESS);
    REQUIRE(first.value.id == 2);

    // Shrink the band in the DB: 7.0 MHz no longer belongs to it. If the second
    // call re-queried the DB it would land in the gap; returning the cached
    // band 2 proves the boundary was served from cache.
    int rc = sqlite3_exec(f.db(), "UPDATE bands SET start_freq = 7050000 WHERE id = 2", nullptr, nullptr, nullptr);
    REQUIRE(rc == SQLITE_OK);

    BandInfoLoadResult second = BandsTable::get_by_freq(7'000'000);
    REQUIRE(second.rc == SUCCESS);
    REQUIRE(second.value.id == 2);
    REQUIRE(second.value.name == "40m CW");
    REQUIRE(second.value.start_freq == 7'000'000);
}

TEST_CASE("BandsTable all_bands returns every band row and its fields", "[db]") {
    BandsTableFixture f;

    std::vector<BandInfo> bands = BandsTable::all_bands();
    REQUIRE(bands.size() == 4);

    // Inactive "GAP" band is included too (used for highlighting later).
    CHECK(bands[0].id == 1);
    CHECK(bands[0].name == "80m");
    CHECK(bands[1].id == 4);
    CHECK(bands[1].name == "GAP");
    CHECK(bands[2].id == 2);
    CHECK(bands[2].name == "40m CW");
    CHECK(bands[3].id == 3);
    CHECK(bands[3].name == "40m SSB");
}

TEST_CASE("BandsTable all_bands returns bands ordered by start frequency", "[db]") {
    BandsTableFixture f;

    std::vector<BandInfo> bands = BandsTable::all_bands();
    REQUIRE(bands.size() == 4);

    // The values were inserted out of order; the query must sort them by
    // start_freq so the band edges screen can draw them left-to-right.
    CHECK(bands[0].start_freq == 3'500'000);
    CHECK(bands[1].start_freq == 5'000'000);
    CHECK(bands[2].start_freq == 7'000'000);
    CHECK(bands[3].start_freq == 7'050'000);

    // Frequencies and types must be copied verbatim.
    CHECK(bands[0].stop_freq == 4'000'000);
    CHECK(bands[0].type == BAND_ACTIVE);
    CHECK(bands[1].type == BAND_INACTIVE);
    CHECK(bands[2].type == BAND_ACTIVE);
    CHECK(bands[3].stop_freq == 7'200'000);
    CHECK(bands[3].type == BAND_ACTIVE);
}

TEST_CASE("BandsTable next goes from a gap to the next band upwards", "[db]") {
    BandsTableFixture f;

    // GAP (inactive, 5-6 MHz) -> the first active band above it is 40m CW.
    BandInfoLoadResult res = BandsTable::next(4, 5'500'000, true);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 2);
    CHECK(res.value.name == "40m CW");
    CHECK(res.value.start_freq == 7'000'000);
    CHECK(res.value.stop_freq == 7'050'000);
}

TEST_CASE("BandsTable next goes from an unusable frequency up to the next band", "[db]") {
    BandsTableFixture f;

    // 6.5 MHz is between the end of 80m and the start of 40m CW. Although the
    // band is inactive, next() must return 40m CW.
    BandInfoLoadResult res = BandsTable::next(BAND_UNDEFINED, 6'500'000, true);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 2);
    CHECK(res.value.name == "40m CW");
}

TEST_CASE("BandsTable next jumps from a band over the adjacent shared boundary upwards", "[db]") {
    BandsTableFixture f;

    // 7.05 MHz is the shared start of 40m CW and 40m SSB; starting from 40m CW
    // going up must skip the current band and land on 40m SSB.
    BandInfoLoadResult res = BandsTable::next(2, 7'050'000, true);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 3);
    CHECK(res.value.name == "40m SSB");
    CHECK(res.value.start_freq == 7'050'000);
    CHECK(res.value.stop_freq == 7'200'000);
}

TEST_CASE("BandsTable next goes from the bottom edge upwards to the first band", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::next(BAND_UNDEFINED, 1'000'000, true);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 1);
    CHECK(res.value.name == "80m");
}

TEST_CASE("BandsTable next returns NOT_FOUND above the last band", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::next(3, 7'200'000, true);
    CHECK(res.rc == NOT_FOUND);
}

TEST_CASE("BandsTable next goes to the previous band downwards", "[db]") {
    BandsTableFixture f;

    // 40m CW -> 80m.
    BandInfoLoadResult res = BandsTable::next(2, 7'010'000, false);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 1);
    CHECK(res.value.name == "80m");
    CHECK(res.value.start_freq == 3'500'000);
    CHECK(res.value.stop_freq == 4'000'000);
}

TEST_CASE("BandsTable next goes down from the adjacent to the previous band", "[db]") {
    BandsTableFixture f;

    // 7.05 MHz inside 40m SSB; moving downwards must land on 40m CW.
    BandInfoLoadResult res = BandsTable::next(3, 7'050'000, false);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 2);
    CHECK(res.value.name == "40m CW");
}

TEST_CASE("BandsTable next goes from a gap down to the previous band", "[db]") {
    BandsTableFixture f;

    // Gap band (5-6 MHz) downwards hits the active band below it (80m).
    BandInfoLoadResult res = BandsTable::next(4, 5'500'000, false);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 1);
    CHECK(res.value.name == "80m");
}

TEST_CASE("BandsTable next returns NOT_FOUND below the first band", "[db]") {
    BandsTableFixture f;

    BandInfoLoadResult res = BandsTable::next(1, 3'500'000, false);
    CHECK(res.rc == NOT_FOUND);
}

TEST_CASE("BandsTable next skips the current band even on an inactive band", "[db]") {
    BandsTableFixture f;

    // From the gap row (id=4) moving down reaches 80m, not the GAP itself.
    BandInfoLoadResult res = BandsTable::next(4, 5'500'000, false);
    CHECK(res.rc == SUCCESS);
    CHECK(res.value.id == 1);
}
