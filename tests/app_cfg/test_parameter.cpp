// test_parameter.cpp
// Tests for Parameter<ValueType, DbType>: validation, deferred-write enqueue,
// set_quiet without enqueue, DbType conversion (scaling) and DB round-trip
// using the flat `params` table over an in-memory SQLite connection.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <optional>
#include <string>
#include <sqlite3.h>

#include "parameter.h"
#include "db.h"
#include "tests/app_cfg/mocks/mock_pending_writes.h"

// Subject/Observer types live in namespace appcfg (see subject.h).
using namespace appcfg;

namespace {

// RAII wrapper for the shared in-memory DB used by the round-trip tests.
// Creates the params table, initialises the shared prepared statements and
// tears everything down on scope exit (also cleanly shuts down the shared
// connection so ASan/UBSan runs do not leak).
struct TestDbGuard {
    sqlite3* db = nullptr;

    TestDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char* err = nullptr;
        int rc = sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS params ("
            "  name TEXT PRIMARY KEY,"
            "  val  TEXT NOT NULL"
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

TEST_CASE("Parameter set validates and enqueues", "[parameter]") {
    MockWriteSink sink;
    Parameter<int, int> vol("volume", 50, StorageType::GLOBAL, sink,
                            [](int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); });

    vol.set(75);
    REQUIRE(vol.get() == 75);
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].key.name == "volume");
    REQUIRE(sink.records[0].key.type == StorageType::GLOBAL);
    REQUIRE(sink.records[0].kind == MockWriteSink::Record::Kind::Int);
    REQUIRE(sink.records[0].value_int == 75);
}

TEST_CASE("Parameter BAND set uses bound context_id", "[parameter]") {
    MockWriteSink sink;
    // Band parameter enqueues into the band context after set_context_id.
    Parameter<int, int32_t> p("vfoa_freq", 7, StorageType::BAND, sink);

    // Default context 0 until SettingsManager binds it.
    p.set(10);
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].key.context_id == 0);

    // After binding to band 2, writes target that band.
    p.set_context_id(2);
    p.set(20);
    REQUIRE(sink.records.size() == 2);
    REQUIRE(sink.records[1].key.context_id == 2);

    // load()/save() default to the bound context_id.
    REQUIRE(p.context_id() == 2);
}

TEST_CASE("Parameter validator clamps invalid values", "[parameter]") {
    MockWriteSink sink;
    Parameter<int, int32_t> p("volume", 50, StorageType::GLOBAL, sink,
        [](int value) { return value < 0 ? 0 : (value > 100 ? 100 : value); });

    p.set(-10);
    REQUIRE(p.get() == 0);
    p.set(200);
    REQUIRE(p.get() == 100);
    // Setting the current value again neither notifies nor enqueues.
    p.set(100);
    REQUIRE(p.get() == 100);
    REQUIRE(sink.records.size() == 2);
}

TEST_CASE("Parameter notifies and enqueues only on change", "[parameter]") {
    MockWriteSink sink;
    Parameter<int, int32_t> p("volume", 50, StorageType::GLOBAL, sink);
    int notify_count = 0;
    auto count_cb = [](Subject* s, void* ud) { ++(*static_cast<int*>(ud)); };
    Subscription sub(p.subscribe(count_cb, &notify_count));

    p.set(75);
    REQUIRE(notify_count == 1);
    REQUIRE(sink.records.size() == 1);

    // Same value after validation -> no notification and no enqueue.
    p.set(75);
    REQUIRE(notify_count == 1);
    REQUIRE(sink.records.size() == 1);

    p.set(80);
    REQUIRE(notify_count == 2);
    REQUIRE(sink.records.size() == 2);
}

TEST_CASE("Parameter set_quiet does not enqueue", "[parameter]") {
    MockWriteSink sink;
    Parameter<int, int32_t> p("volume", 50, StorageType::GLOBAL, sink);
    p.set_quiet(80);
    REQUIRE(p.get() == 80);
    REQUIRE(sink.records.empty());
}

TEST_CASE("Parameter float->int32 scaling round-trip", "[parameter]") {
    TestDbGuard db;
    MockWriteSink sink;
    // Frequency in MHz stored as Hz x1000 in DB.
    Parameter<float, int32_t> p("vfoa_freq", 7.100f, StorageType::BAND, sink);

    p.set(14.200f);
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].value_int == 14200);

    REQUIRE(p.save() == SUCCESS);
    REQUIRE(p.load() == SUCCESS);
    // 14200 / 1000.0 == 14.2
    REQUIRE(p.get() == Catch::Approx(14.2f));
}

TEST_CASE("Parameter custom NTTP Scale scales enqueue and round-trip", "[parameter]") {
    TestDbGuard db;
    MockWriteSink sink;
    // Custom compile-time scale x10 (e.g. tenths stored as int).
    Parameter<float, int32_t, 10> p("custom_scale", 0.0f, StorageType::BAND, sink);

    p.set(2.5f);
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].value_int == 25);

    REQUIRE(p.save() == SUCCESS);
    REQUIRE(p.load() == SUCCESS);
    // 25 / 10.0 == 2.5
    REQUIRE(p.get() == Catch::Approx(2.5f));
}

TEST_CASE("Parameter Scale is a compile-time constant per instantiation", "[parameter]") {
    MockWriteSink sink;
    // Two parameters with different compile-time scales do not interfere.
    Parameter<float, int32_t, 10> p10("p10", 0.0f, StorageType::BAND, sink);
    Parameter<float, int32_t, 2>  p2("p2", 0.0f, StorageType::BAND, sink);

    p10.set(1.0f);
    p2.set(1.0f);
    REQUIRE(sink.records.size() == 2);
    REQUIRE(sink.records[0].value_int == 10);
    REQUIRE(sink.records[1].value_int == 2);
}

TEST_CASE("Parameter save/load round-trip int32", "[parameter][storage]") {
    TestDbGuard db;
    MockWriteSink sink;
    Parameter<int, int32_t> p("volume", 0, StorageType::GLOBAL, sink);

    p.set(90);
    REQUIRE(p.save() == SUCCESS);

    // Fresh parameter loads the persisted value.
    Parameter<int, int32_t> loaded("volume", 0, StorageType::GLOBAL, sink);
    REQUIRE(loaded.load() == SUCCESS);
    REQUIRE(loaded.get() == 90);
}

TEST_CASE("Parameter load NOT_FOUND keeps current value", "[parameter]") {
    TestDbGuard db;
    MockWriteSink sink;
    bool not_found_called = false;

    Parameter<int, int32_t> p("missing_param", 7, StorageType::GLOBAL, sink,
                              {}, [&]() { not_found_called = true; });
    REQUIRE(p.load() == NOT_FOUND);
    REQUIRE(p.get() == 7);
    REQUIRE(not_found_called);
}

TEST_CASE("Parameter save/load text round-trip", "[parameter]") {
    TestDbGuard db;
    MockWriteSink sink;
    Parameter<std::string, std::string> p("callsign", "UU0XXX", StorageType::GLOBAL, sink);

    p.set("R2ABC");
    REQUIRE(p.get() == "R2ABC");
    REQUIRE(p.save() == SUCCESS);

    Parameter<std::string, std::string> loaded("callsign", "", StorageType::GLOBAL, sink);
    REQUIRE(loaded.load() == SUCCESS);
    REQUIRE(loaded.get() == "R2ABC");
}

TEST_CASE("Parameter self-registers into a ParamBase group", "[parameter]") {
    MockWriteSink sink;
    std::vector<ParamBase*> group;
    Parameter<int32_t> a("a", 0, StorageType::GLOBAL, sink, nullptr, {}, &group);
    Parameter<float, int32_t, 10> b("b", 0.0f, StorageType::BAND, sink, nullptr, {}, &group);
    REQUIRE(group.size() == 2);
    REQUIRE(std::string(group[0]->db_name()) == "a");
    REQUIRE(group[0]->storage_type() == StorageType::GLOBAL);
    REQUIRE(std::string(group[1]->db_name()) == "b");
    REQUIRE(group[1]->storage_type() == StorageType::BAND);
}
