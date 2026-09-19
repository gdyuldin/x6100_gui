// test_transverter.cpp
// Tests for the transverter migration: KeyValueTable<TRANSVERTER> DB
// round-trips against the legacy `transverter` schema, the TRANSVERTER routing
// through store_load/store_save and the PendingWrites flush path. The DB
// fixture replicates the production schema from sql/params.sql so existing user
// data is compatible.
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <string>

#include "db.h"
#include "parameter.h"
#include "pending_writes.h"
#include "tests/cfg/mocks/pending_writes_test_access.h"

// RAII fixture: opens an in-memory database, creates the transverter table and
// initialises KeyValueTable<TRANSVERTER>. Shuts it down and closes the
// connection on destruction.
class TransverterKeyValueTableFixture {
  public:
    TransverterKeyValueTableFixture() {
        int rc;
        rc = sqlite3_open(":memory:", &db_);
        REQUIRE(rc == SQLITE_OK);

        const char *create_sql = "CREATE TABLE transverter("
                                 "    id     INTEGER,"
                                 "    name   TEXT,"
                                 "    val    INTEGER,"
                                 "    UNIQUE(id, name) ON CONFLICT REPLACE"
                                 ");";
        rc                     = sqlite3_exec(db_, create_sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        bool ok = KeyValueTable<StorageType::TRANSVERTER>::Init(db_);
        REQUIRE(ok);
    }

    ~TransverterKeyValueTableFixture() {
        KeyValueTable<StorageType::TRANSVERTER>::Shutdown();
        sqlite3_close(db_);
    }

    sqlite3 *db() { return db_; }

  private:
    sqlite3 *db_ = nullptr;
};

// ---------------------------------------------------------------------------
// KeyValueTable<TRANSVERTER>
// ---------------------------------------------------------------------------

TEST_CASE("TransverterKeyValueTable saves and loads int32_t per transverter", "[transverter]") {
    TransverterKeyValueTableFixture f;

    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(0, "from", 144'000'000) == SUCCESS);
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(1, "from", 432'000'000) == SUCCESS);

    ParamLoadResult<int32_t> res0 = KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(0, "from");
    REQUIRE(res0.rc == SUCCESS);
    REQUIRE(res0.value == 144'000'000);

    ParamLoadResult<int32_t> res1 = KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(1, "from");
    REQUIRE(res1.rc == SUCCESS);
    REQUIRE(res1.value == 432'000'000);
}

TEST_CASE("TransverterKeyValueTable overwrites a value under the same (id, name) key", "[transverter]") {
    TransverterKeyValueTableFixture f;

    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(0, "shift", 116'000'000) == SUCCESS);
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(0, "shift").value == 116'000'000);

    // INSERT OR REPLACE on (id, name) must replace the stored value.
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(0, "shift", 42'000'000) == SUCCESS);
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(0, "shift").value == 42'000'000);
}

TEST_CASE("TransverterKeyValueTable isolates values between transverters", "[transverter]") {
    TransverterKeyValueTableFixture f;

    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(0, "shift", 116'000'000) == SUCCESS);
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(1, "shift", 404'000'000) == SUCCESS);

    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(0, "shift").value == 116'000'000);
    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(1, "shift").value == 404'000'000);
}

TEST_CASE("TransverterKeyValueTable returns NOT_FOUND for a missing key", "[transverter]") {
    TransverterKeyValueTableFixture f;

    ParamLoadResult<int32_t> res = KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(0, "missing");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("TransverterKeyValueTable returns NOT_FOUND when the id has no entry", "[transverter]") {
    TransverterKeyValueTableFixture f;

    REQUIRE(KeyValueTable<StorageType::TRANSVERTER>::Save<int32_t>(0, "from", 144'000'000) == SUCCESS);

    ParamLoadResult<int32_t> res = KeyValueTable<StorageType::TRANSVERTER>::Load<int32_t>(1, "from");
    REQUIRE(res.rc == NOT_FOUND);
}

// ---------------------------------------------------------------------------
// store_load/store_save route TRANSVERTER to the transverter table
// ---------------------------------------------------------------------------

TEST_CASE("store_save/store_load route TRANSVERTER through the real DB", "[transverter]") {
    TransverterKeyValueTableFixture f;

    REQUIRE(store_save<int32_t>(StorageType::TRANSVERTER, 0, "from", 145'000'000) == SUCCESS);

    std::optional<int32_t> val = store_load<int32_t>(StorageType::TRANSVERTER, 0, "from");
    REQUIRE(val.has_value());
    REQUIRE(*val == 145'000'000);

    std::optional<int32_t> missing = store_load<int32_t>(StorageType::TRANSVERTER, 1, "from");
    REQUIRE(!missing.has_value());
}

// ---------------------------------------------------------------------------
// PendingWrites: TRANSVERTER entries are flushed by flush_all()
// ---------------------------------------------------------------------------

TEST_CASE("PendingWrites flush_all persists TRANSVERTER entries", "[transverter]") {
    TransverterKeyValueTableFixture f;
    PendingWrites                   pending;

    StorageKey key{StorageType::TRANSVERTER, 0, "from"};
    pending.write(key, int32_t(144'000'000));

    pending.flush_all();

    // flush_all() must cover the TRANSVERTER table: the pending entry is gone
    // and the value is in the real DB.
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(store_load<int32_t>(StorageType::TRANSVERTER, 0, "from").value() == 144'000'000);
}

TEST_CASE("PendingWrites flush_all keeps TRANSVERTER entry on failed save", "[transverter]") {
    // The transverter table is intentionally left uninitialised so the first
    // flush fails and must retain the pending entry for a later retry.
    PendingWrites pending;

    StorageKey key{StorageType::TRANSVERTER, 1, "shift"};
    pending.write(key, int32_t(404'000'000));

    pending.flush_all();
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 404'000'000);

    // Initialise the table and retry: the save succeeds and clears the entry.
    TransverterKeyValueTableFixture f;
    pending.flush_all();
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(store_load<int32_t>(StorageType::TRANSVERTER, 1, "shift").value() == 404'000'000);
}
