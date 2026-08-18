// test_transverter.cpp
// Tests for the transverter migration: TransverterTable DB round-trips against
// the legacy `transverter` schema, and the TRANSVERTER storage-policy path
// (TransverterStorage + PendingWrites flush). The DB fixture replicates the
// production schema from sql/params.sql so existing user data is compatible.
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <string>

#include "db.h"
#include "parameter.h"
#include "pending_writes.h"
#include "storage_policy.h"
#include "tests/cfg/mocks/mock_storage.h"
#include "tests/cfg/mocks/pending_writes_test_access.h"

// RAII fixture: opens an in-memory database, creates the transverter table and
// initialises TransverterTable. Shuts it down and closes the connection on
// destruction.
class TransverterTableFixture {
  public:
    TransverterTableFixture() {
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

        bool ok = TransverterTable::Init(db_);
        REQUIRE(ok);
    }

    ~TransverterTableFixture() {
        TransverterTable::Shutdown();
        sqlite3_close(db_);
    }

    sqlite3 *db() { return db_; }

  private:
    sqlite3 *db_ = nullptr;
};

// ---------------------------------------------------------------------------
// TransverterTable
// ---------------------------------------------------------------------------

TEST_CASE("TransverterTable saves and loads int32_t per transverter", "[transverter]") {
    TransverterTableFixture f;

    REQUIRE(TransverterTable::Save<int32_t>(0, "from", 144'000'000) == SUCCESS);
    REQUIRE(TransverterTable::Save<int32_t>(1, "from", 432'000'000) == SUCCESS);

    ParamLoadResult<int32_t> res0 = TransverterTable::Load<int32_t>(0, "from");
    REQUIRE(res0.rc == SUCCESS);
    REQUIRE(res0.value == 144'000'000);

    ParamLoadResult<int32_t> res1 = TransverterTable::Load<int32_t>(1, "from");
    REQUIRE(res1.rc == SUCCESS);
    REQUIRE(res1.value == 432'000'000);
}

TEST_CASE("TransverterTable overwrites a value under the same (id, name) key", "[transverter]") {
    TransverterTableFixture f;

    REQUIRE(TransverterTable::Save<int32_t>(0, "shift", 116'000'000) == SUCCESS);
    REQUIRE(TransverterTable::Load<int32_t>(0, "shift").value == 116'000'000);

    // INSERT OR REPLACE on (id, name) must replace the stored value.
    REQUIRE(TransverterTable::Save<int32_t>(0, "shift", 42'000'000) == SUCCESS);
    REQUIRE(TransverterTable::Load<int32_t>(0, "shift").value == 42'000'000);
}

TEST_CASE("TransverterTable isolates values between transverters", "[transverter]") {
    TransverterTableFixture f;

    REQUIRE(TransverterTable::Save<int32_t>(0, "shift", 116'000'000) == SUCCESS);
    REQUIRE(TransverterTable::Save<int32_t>(1, "shift", 404'000'000) == SUCCESS);

    REQUIRE(TransverterTable::Load<int32_t>(0, "shift").value == 116'000'000);
    REQUIRE(TransverterTable::Load<int32_t>(1, "shift").value == 404'000'000);
}

TEST_CASE("TransverterTable returns NOT_FOUND for a missing key", "[transverter]") {
    TransverterTableFixture f;

    ParamLoadResult<int32_t> res = TransverterTable::Load<int32_t>(0, "missing");
    REQUIRE(res.rc == NOT_FOUND);
}

TEST_CASE("TransverterTable returns NOT_FOUND when the id has no entry", "[transverter]") {
    TransverterTableFixture f;

    REQUIRE(TransverterTable::Save<int32_t>(0, "from", 144'000'000) == SUCCESS);

    ParamLoadResult<int32_t> res = TransverterTable::Load<int32_t>(1, "from");
    REQUIRE(res.rc == NOT_FOUND);
}

// ---------------------------------------------------------------------------
// TransverterStorage (routes to TransverterTable through the real DB)
// ---------------------------------------------------------------------------

TEST_CASE("TransverterStorage saves and loads int32_t via TransverterTable", "[transverter]") {
    TransverterTableFixture f;

    TransverterStorage storage;

    int rc = storage.save_int(0, "from", 145'000'000);
    REQUIRE(rc == SUCCESS);

    std::optional<int32_t> val = storage.load_int(0, "from");
    REQUIRE(val.has_value());
    REQUIRE(*val == 145'000'000);

    std::optional<int32_t> missing = storage.load_int(1, "from");
    REQUIRE(!missing.has_value());
}

// ---------------------------------------------------------------------------
// PendingWrites: TRANSVERTER entries are flushed by flush_all()
// ---------------------------------------------------------------------------

TEST_CASE("PendingWrites flush_all persists TRANVERTER entries", "[transverter]") {
    // One mock bound to StorageType::TRANSVERTER (mirrors TransverterStorage).
    MockStorage   mock_tv(StorageType::TRANSVERTER);
    PendingWrites pending([&](StorageType type) -> StoragePolicy & {
        (void)type;
        return mock_tv;
    });

    StorageKey key{StorageType::TRANSVERTER, 0, "from"};
    pending.write(key, int32_t(144'000'000));

    pending.flush_all();

    // flush_all() must cover the TRANSVERTER table: the pending entry is gone
    // and the mock persisted it.
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(mock_tv.ints().at(key) == 144'000'000);
}

TEST_CASE("PendingWrites flush_all keeps TRANSVERTER entry on failed save", "[transverter]") {
    MockStorage   mock_tv(StorageType::TRANSVERTER);
    PendingWrites pending([&](StorageType type) -> StoragePolicy & {
        (void)type;
        return mock_tv;
    });

    StorageKey key{StorageType::TRANSVERTER, 1, "shift"};
    pending.write(key, int32_t(404'000'000));

    mock_tv.arm_fail_save(5);
    pending.flush_all();

    // The failed save must NOT drop the pending entry (retry later).
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 404'000'000);

    // A subsequent flush succeeds.
    pending.flush_all();
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(mock_tv.ints().at(key) == 404'000'000);
}
