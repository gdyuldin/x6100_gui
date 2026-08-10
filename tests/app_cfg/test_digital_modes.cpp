// test_digital_modes.cpp
// Tests for DigitalModesTable: find_next/find_closest/find_prev navigation
// over the digital_modes table (FT8/FT4 presets) against an in-memory SQLite
// database. The fixture creates the production schema from sql/params.sql and
// seeds the real 20 presets from sql/digital_modes.csv.
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <string>

#include "db.h"
#include "digital_modes.h"

// RAII fixture: opens an in-memory database, creates the digital_modes table
// (matching sql/params.sql) with the production preset rows and initialises
// DigitalModesTable. Shuts the table down and closes the connection on
// destruction.
class DigitalModesTableFixture {
  public:
    DigitalModesTableFixture() {
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char *create_sql = "CREATE TABLE digital_modes("
                                 "    id     INTEGER PRIMARY KEY AUTOINCREMENT,"
                                 "    label  varchar(64) NOT NULL,"
                                 "    freq   INTEGER NOT NULL CHECK(freq > 0),"
                                 "    mode   INTEGER NOT NULL DEFAULT 3 CHECK(mode >= 0 AND mode <= 7),"
                                 "    type   INTEGER NOT NULL,"
                                 "    CONSTRAINT freq_type_uniq UNIQUE(freq, type)"
                                 ");";
        rc                     = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        // Seed the table with the production presets from sql/digital_modes.csv
        // (11 FT8 rows, then 9 FT4 rows; every mode is 3 = x6100_mode_usb_dig).
        const char *insert_sql = "INSERT INTO digital_modes(label, freq, mode, type) VALUES"
                                 "    ('FT8 160m', 1840000, 3, 0),"
                                 "    ('FT8 80m',  3573000, 3, 0),"
                                 "    ('FT8 60m',  5357000, 3, 0),"
                                 "    ('FT8 40m',  7074000, 3, 0),"
                                 "    ('FT8 30m',  10136000, 3, 0),"
                                 "    ('FT8 20m',  14074000, 3, 0),"
                                 "    ('FT8 17m',  18100000, 3, 0),"
                                 "    ('FT8 15m',  21074000, 3, 0),"
                                 "    ('FT8 12m',  24915000, 3, 0),"
                                 "    ('FT8 10m',  28074000, 3, 0),"
                                 "    ('FT8 6m',   50313000, 3, 0),"
                                 "    ('FT4 80m',  3575000, 3, 1),"
                                 "    ('FT4 40m',  7047500, 3, 1),"
                                 "    ('FT4 30m',  10140000, 3, 1),"
                                 "    ('FT4 20m',  14080000, 3, 1),"
                                 "    ('FT4 17m',  18104000, 3, 1),"
                                 "    ('FT4 15m',  21140000, 3, 1),"
                                 "    ('FT4 12m',  24919000, 3, 1),"
                                 "    ('FT4 10m',  28180000, 3, 1),"
                                 "    ('FT4 6m',   50318000, 3, 1);";
        rc                     = sqlite3_exec(db_, insert_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = DigitalModesTable::Init(db_);
        REQUIRE(ok);
    }

    ~DigitalModesTableFixture() {
        DigitalModesTable::Shutdown();
        sqlite3_close(db_);
    }

    sqlite3 *db() { return db_; }

  private:
    sqlite3 *db_ = nullptr;
};

TEST_CASE("DigitalModesTable find_next returns the next preset", "[digital_modes]") {
    DigitalModesTableFixture f;

    // From the FT8 160m preset (1.84 MHz) the next FT8 preset is 80m.
    DigitalModesTable::LoadResult res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 1'840'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);
    REQUIRE(res.value.label == "FT8 80m");
}

TEST_CASE("DigitalModesTable find_next returns NOT_FOUND above the last preset", "[digital_modes]") {
    DigitalModesTableFixture f;

    // 52 MHz is above every FT8 preset (the highest is 6m at 50.313 MHz).
    DigitalModesTable::LoadResult res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 52'000'000);
    REQUIRE(res.rc == NOT_FOUND);

    // Also NOT_FOUND when the current frequency equals the top preset (the
    // query uses a strict >).
    res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 50'313'000);
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("DigitalModesTable find_prev returns the previous preset", "[digital_modes]") {
    DigitalModesTableFixture f;

    // From the FT8 20m preset (14.074 MHz) the previous FT8 preset is 30m.
    DigitalModesTable::LoadResult res = DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, 14'074'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 10'136'000);
    REQUIRE(res.value.label == "FT8 30m");
}

TEST_CASE("DigitalModesTable find_prev returns NOT_FOUND below the first preset", "[digital_modes]") {
    DigitalModesTableFixture f;

    // Nothing below the FT8 160m preset; the strict < misses equality too.
    DigitalModesTable::LoadResult res = DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, 1'840'000);
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("DigitalModesTable find_closest selects the nearest preset", "[digital_modes]") {
    DigitalModesTableFixture f;

    // 2.5 MHz is closer to 160m (1.84 MHz, |d|=660k) than to 80m (3.573 MHz).
    DigitalModesTable::LoadResult res = DigitalModesTable::find_closest(APP_CFG_DIG_TYPE_FT8, 2'500'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 1'840'000);

    // 3.0 MHz is closer to 80m (3.573 MHz, |d|=573k) than to 160m (1.84 MHz).
    res = DigitalModesTable::find_closest(APP_CFG_DIG_TYPE_FT8, 3'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);
}

TEST_CASE("DigitalModesTable isolates FT8 and FT4 presets", "[digital_modes]") {
    DigitalModesTableFixture f;

    // The FT4 80m preset (3.575 MHz) sits between the FT8 80m (3.573 MHz) and
    // FT8 60m (5.357 MHz) presets; navigation must skip it.
    DigitalModesTable::LoadResult res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 3'573'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 5'357'000);
    REQUIRE(res.value.label == "FT8 60m");

    // Same boundary from the prev direction.
    res = DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, 5'357'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);

    // FT4 navigation still finds its own rows interleaved with the FT8 ones.
    res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT4, 3'575'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 7'047'500);
}

TEST_CASE("DigitalModesTable returns NOT_FOUND for an empty table", "[digital_modes]") {
    DigitalModesTableFixture f;

    int rc = sqlite3_exec(f.db(), "DELETE FROM digital_modes", nullptr, nullptr, nullptr);
    REQUIRE(rc == SQLITE_OK);

    REQUIRE(DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 1'840'000).rc == NOT_FOUND);
    REQUIRE(DigitalModesTable::find_closest(APP_CFG_DIG_TYPE_FT8, 7'074'000).rc == NOT_FOUND);
    REQUIRE(DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, 7'074'000).rc == NOT_FOUND);
}

TEST_CASE("DigitalModesTable navigates up/down/up in sequence", "[digital_modes]") {
    DigitalModesTableFixture f;

    // Simulate the FT8 dialog band-up: 160m -> 80m -> 60m.
    DigitalModesTable::LoadResult res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 1'840'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);

    res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, res.value.freq);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 5'357'000);

    // One step back down.
    res = DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, res.value.freq);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);

    // Closest from the returned preset lands back on itself.
    res = DigitalModesTable::find_closest(APP_CFG_DIG_TYPE_FT8, res.value.freq);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 3'573'000);
}

TEST_CASE("DigitalModesTable stores the preset label", "[digital_modes]") {
    DigitalModesTableFixture f;

    DigitalModesTable::LoadResult res = DigitalModesTable::find_prev(APP_CFG_DIG_TYPE_FT8, 5'357'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.label == "FT8 80m");
}

TEST_CASE("DigitalModesTable loads the mode column", "[digital_modes]") {
    DigitalModesTableFixture f;

    // All presets are stored with mode 3 (x6100_mode_usb_dig / DATA-USB).
    DigitalModesTable::LoadResult res = DigitalModesTable::find_next(APP_CFG_DIG_TYPE_FT8, 1'000'000);
    REQUIRE(res.rc == SUCCESS);
    REQUIRE(res.value.freq == 1'840'000);
    REQUIRE(res.value.mode == 3);
}
