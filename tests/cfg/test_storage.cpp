// test_storage.cpp
// Tests for the store_load/store_save routing helpers: round-trip save/load
// against an in-memory SQLite connection, per-value-type correctness
// (int32/float/string), nullopt on a missing key and context_id isolation for
// the band/mode/transverter stores.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>
#include <string>

#include "db.h"

namespace {

// RAII wrapper for the in-memory DB. Creates all four key-value tables with the
// same schema as sql/params.sql and initialises the shared prepared statements
// of every KeyValueTable instantiation used by store_load/store_save.
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
                                    "CREATE TABLE IF NOT EXISTS transverter("
                                    "  id   INTEGER,"
                                    "  name TEXT,"
                                    "  val  INTEGER,"
                                    "  UNIQUE (id, name) ON CONFLICT REPLACE"
                                    ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
        KeyValueTable<StorageType::GLOBAL>::Init(db);
        KeyValueTable<StorageType::BAND>::Init(db);
        KeyValueTable<StorageType::MODE>::Init(db);
        KeyValueTable<StorageType::TRANSVERTER>::Init(db);
    }

    ~TestDbGuard() {
        KeyValueTable<StorageType::GLOBAL>::Shutdown();
        KeyValueTable<StorageType::BAND>::Shutdown();
        KeyValueTable<StorageType::MODE>::Shutdown();
        KeyValueTable<StorageType::TRANSVERTER>::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

TEST_CASE("store_save/store_load GLOBAL round-trip int32/float/string", "[storage]") {
    TestDbGuard db;

    REQUIRE(store_save<int32_t>(StorageType::GLOBAL, 0, "volume", 75) == SUCCESS);
    auto v_int = store_load<int32_t>(StorageType::GLOBAL, 0, "volume");
    REQUIRE(v_int.has_value());
    REQUIRE(*v_int == 75);

    REQUIRE(store_save<float>(StorageType::GLOBAL, 0, "squelch", 3.5f) == SUCCESS);
    auto v_float = store_load<float>(StorageType::GLOBAL, 0, "squelch");
    REQUIRE(v_float.has_value());
    REQUIRE(*v_float == Catch::Approx(3.5f));

    REQUIRE(store_save<std::string>(StorageType::GLOBAL, 0, "callsign", "R2ABC") == SUCCESS);
    auto v_text = store_load<std::string>(StorageType::GLOBAL, 0, "callsign");
    REQUIRE(v_text.has_value());
    REQUIRE(*v_text == "R2ABC");
}

TEST_CASE("store_load of a missing key returns nullopt", "[storage]") {
    TestDbGuard db;

    REQUIRE_FALSE(store_load<int32_t>(StorageType::GLOBAL, 0, "no_such_param").has_value());
    REQUIRE_FALSE(store_load<float>(StorageType::GLOBAL, 0, "no_such_param").has_value());
    REQUIRE_FALSE(store_load<std::string>(StorageType::GLOBAL, 0, "no_such_param").has_value());
}

TEST_CASE("BAND store round-trip isolates context_id", "[storage]") {
    TestDbGuard db;

    REQUIRE(store_save<int32_t>(StorageType::BAND, 5, "vfoa_freq", 14'200'000) == SUCCESS);
    REQUIRE(store_save<int32_t>(StorageType::BAND, 6, "vfoa_freq", 14'300'000) == SUCCESS);

    auto v5 = store_load<int32_t>(StorageType::BAND, 5, "vfoa_freq");
    auto v6 = store_load<int32_t>(StorageType::BAND, 6, "vfoa_freq");
    REQUIRE(v5.has_value());
    REQUIRE(v6.has_value());
    REQUIRE(*v5 == 14'200'000);
    REQUIRE(*v6 == 14'300'000);

    // Missing name in an existing context still returns nullopt.
    REQUIRE_FALSE(store_load<int32_t>(StorageType::BAND, 5, "no_such_param").has_value());
}

TEST_CASE("BAND store round-trip float and string", "[storage]") {
    TestDbGuard db;

    REQUIRE(store_save<float>(StorageType::BAND, 5, "tone", 600.0f) == SUCCESS);
    auto v_float = store_load<float>(StorageType::BAND, 5, "tone");
    REQUIRE(v_float.has_value());
    REQUIRE(*v_float == Catch::Approx(600.0f));

    REQUIRE(store_save<std::string>(StorageType::BAND, 5, "label", "40m") == SUCCESS);
    auto v_text = store_load<std::string>(StorageType::BAND, 5, "label");
    REQUIRE(v_text.has_value());
    REQUIRE(*v_text == "40m");
}

TEST_CASE("MODE store round-trip with mode context_id", "[storage]") {
    TestDbGuard db;

    REQUIRE(store_save<int32_t>(StorageType::MODE, 3, "squelch", 12) == SUCCESS);
    auto v = store_load<int32_t>(StorageType::MODE, 3, "squelch");
    REQUIRE(v.has_value());
    REQUIRE(*v == 12);

    // A different mode does not see the value.
    REQUIRE_FALSE(store_load<int32_t>(StorageType::MODE, 4, "squelch").has_value());

    REQUIRE(store_save<float>(StorageType::MODE, 3, "tone", 700.0f) == SUCCESS);
    auto vf = store_load<float>(StorageType::MODE, 3, "tone");
    REQUIRE(vf.has_value());
    REQUIRE(*vf == Catch::Approx(700.0f));

    REQUIRE(store_save<std::string>(StorageType::MODE, 3, "label", "USB") == SUCCESS);
    auto vt = store_load<std::string>(StorageType::MODE, 3, "label");
    REQUIRE(vt.has_value());
    REQUIRE(*vt == "USB");
}

TEST_CASE("TRANSVERTER store round-trip isolates context_id", "[storage]") {
    TestDbGuard db;

    REQUIRE(store_save<int32_t>(StorageType::TRANSVERTER, 0, "from", 144'000'000) == SUCCESS);
    REQUIRE(store_save<int32_t>(StorageType::TRANSVERTER, 1, "from", 432'000'000) == SUCCESS);

    auto v0 = store_load<int32_t>(StorageType::TRANSVERTER, 0, "from");
    auto v1 = store_load<int32_t>(StorageType::TRANSVERTER, 1, "from");
    REQUIRE(v0.has_value());
    REQUIRE(v1.has_value());
    REQUIRE(*v0 == 144'000'000);
    REQUIRE(*v1 == 432'000'000);

    REQUIRE_FALSE(store_load<int32_t>(StorageType::TRANSVERTER, 0, "no_such_param").has_value());
}
