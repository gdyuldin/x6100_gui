// test_pending_writes.cpp
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <vector>

#include "pending_writes.h"
#include "tests/cfg/mocks/pending_writes_test_access.h"

// Helper to create keys easily
StorageKey make_key(StorageType type, int context_id, const char *name) {
    return {type, context_id, name};
}

TEST_CASE("PendingWrites stores and overwrites int32_t", "[pending_writes]") {
    PendingWrites pending;
    auto          key = make_key(StorageType::BAND, 1, "vfoa_freq");
    pending.write(key, int32_t(7'100'000));
    pending.write(key, int32_t(7'200'00));
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 7'200'00);
}

TEST_CASE("PendingWrites stores different types independently", "[pending_writes]") {
    PendingWrites pending;
    auto          key  = make_key(StorageType::GLOBAL, 0, "volume");
    auto          key2 = make_key(StorageType::GLOBAL, 0, "label");

    pending.write(key, int32_t(50));
    pending.write(key2, std::string("Hello"));

    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 50);
    REQUIRE(PendingWritesTestAccess::peek_text(pending, key2) == "Hello");
    // No cross-contamination
    REQUIRE(PendingWritesTestAccess::peek_text(pending, key) == std::nullopt);
}

TEST_CASE("PendingWrites key distinguishes band_id and mode_id", "[pending_writes]") {
    PendingWrites pending;
    auto          key_band1 = make_key(StorageType::BAND, 1, "vfoa_freq");
    auto          key_band2 = make_key(StorageType::BAND, 2, "vfoa_freq");

    pending.write(key_band1, int32_t(7'000'000));
    pending.write(key_band2, int32_t(14'000'000));

    REQUIRE(PendingWritesTestAccess::peek_int(pending, key_band1) == 7'000'000);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key_band2) == 14'000'000);
}

TEST_CASE("PendingWrites clear drops entries without saving", "[pending_writes]") {
    PendingWrites pending;
    auto          key = make_key(StorageType::BAND, 1, "vfoa_freq");
    pending.write(key, int32_t(7'100'000));
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 7'100'000);

    pending.clear();
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
}

TEST_CASE("PendingWrites thread safety basic", "[pending_writes]") {    PendingWrites pending;
    const int     num_threads       = 4;
    const int     writes_per_thread = 100;
    auto          key               = make_key(StorageType::GLOBAL, 0, "counter");

    auto writer = [&](int start) {
        for (int i = 0; i < writes_per_thread; ++i) {
            pending.write(key, int32_t(start + i));
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(writer, t * writes_per_thread);
    }
    for (auto &th : threads)
        th.join();

    // After all writes, the stored value should be the last one written.
    // There's no guarantee which one, but it must be an integer and not a corrupted state.
    auto val = PendingWritesTestAccess::peek_int(pending, key);
    REQUIRE(val.has_value());
}

namespace {

// RAII in-memory DB whose KeyValueTable<BAND> is only initialised on demand.
// Flushing before init() makes the save fail (statements are not prepared),
// which lets the test exercise the retry path without a mock.
struct PendingWritesDbGuard {
    sqlite3 *db = nullptr;

    PendingWritesDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char *err = nullptr;
        int   rc  = sqlite3_exec(db,
                                 "CREATE TABLE IF NOT EXISTS band_params("
                                    "  bands_id INTEGER,"
                                    "  name     TEXT,"
                                    "  val      INTEGER,"
                                    "  UNIQUE (bands_id, name) ON CONFLICT REPLACE"
                                    ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
    }

    void init() { REQUIRE(KeyValueTable<StorageType::BAND>::Init(db)); }

    ~PendingWritesDbGuard() {
        KeyValueTable<StorageType::BAND>::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

TEST_CASE("PendingWrites retains entry on failed save and retries later", "[pending_writes]") {
    PendingWritesDbGuard db; // table intentionally not initialised yet
    PendingWrites        pending;

    auto key = make_key(StorageType::BAND, 1, "vfoa_freq");
    pending.write(key, int32_t(7'100'000));

    // First save fails (no prepared statement): the pending entry must NOT be
    // dropped (no data loss).
    pending.flush_storage(StorageType::BAND, 1);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 7'100'000);

    // A subsequent flush succeeds and clears the entry.
    db.init();
    pending.flush_storage(StorageType::BAND, 1);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(store_load<int32_t>(StorageType::BAND, 1, "vfoa_freq").value() == 7'100'000);
}
