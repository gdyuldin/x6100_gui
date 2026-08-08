// test_pending_writes.cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <optional>

#include "pending_writes.h"
#include "tests/app_cfg/mocks/pending_writes_test_access.h"
#include "tests/app_cfg/mocks/mock_storage.h"


// Helper to create keys easily
StorageKey make_key(StorageType type, int context_id, const char* name) {
    return {type, context_id, name};
}

TEST_CASE("PendingWrites stores and overwrites int32_t", "[pending_writes]") {
    PendingWrites pending;
    auto key = make_key(StorageType::BAND, 1, "vfoa_freq");
    pending.write(key, int32_t(7'100'000));
    pending.write(key, int32_t(7'200'00));
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 7'200'00);
}


TEST_CASE("PendingWrites stores different types independently", "[pending_writes]") {
    PendingWrites pending;
    auto key = make_key(StorageType::GLOBAL, 0, "volume");
    auto key2 = make_key(StorageType::GLOBAL, 0, "label");

    pending.write(key, int32_t(50));
    pending.write(key2, std::string("Hello"));

    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 50);
    REQUIRE(PendingWritesTestAccess::peek_text(pending, key2) == "Hello");
    // No cross-contamination
    REQUIRE(PendingWritesTestAccess::peek_text(pending, key) == std::nullopt);
}

TEST_CASE("PendingWrites key distinguishes band_id and mode_id", "[pending_writes]") {
    PendingWrites pending;
    auto key_band1 = make_key(StorageType::BAND, 1, "vfoa_freq");
    auto key_band2 = make_key(StorageType::BAND, 2, "vfoa_freq");

    pending.write(key_band1, int32_t(7'000'000));
    pending.write(key_band2, int32_t(14'000'000));

    REQUIRE(PendingWritesTestAccess::peek_int(pending, key_band1) == 7'000'000);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key_band2) == 14'000'000);
}

TEST_CASE("PendingWrites thread safety basic", "[pending_writes]") {
    PendingWrites pending;
    const int num_threads = 4;
    const int writes_per_thread = 100;
    auto key = make_key(StorageType::GLOBAL, 0, "counter");

    auto writer = [&](int start) {
        for (int i = 0; i < writes_per_thread; ++i) {
            pending.write(key, int32_t(start + i));
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(writer, t * writes_per_thread);
    }
    for (auto& th : threads) th.join();

    // After all writes, the stored value should be the last one written.
    // There's no guarantee which one, but it must be an integer and not a corrupted state.
    auto val = PendingWritesTestAccess::peek_int(pending, key);
    REQUIRE(val.has_value());
}

TEST_CASE("PendingWrites retains entry on failed save and retries later", "[pending_writes]") {
    // One mock per logical table (each bound to its StorageType), so keys are
    // tagged with the correct type — mirroring the production design where a
    // storage policy instance only ever touches its own table.
    MockStorage mock_band(StorageType::BAND);
    PendingWrites pending(
        [&](StorageType type) -> StoragePolicy& {
            (void)type;
            return mock_band;
        });

    auto key = make_key(StorageType::BAND, 1, "vfoa_freq");
    pending.write(key, int32_t(7'100'000));

    // First save fails: the pending entry must NOT be dropped (no data loss).
    mock_band.arm_fail_save(5);
    pending.flush_storage(StorageType::BAND, 1);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == 7'100'000);
    REQUIRE(mock_band.ints().empty());

    // A subsequent flush succeeds and clears the entry.
    pending.flush_storage(StorageType::BAND, 1);
    REQUIRE(PendingWritesTestAccess::peek_int(pending, key) == std::nullopt);
    REQUIRE(mock_band.ints().at({StorageType::BAND, 1, "vfoa_freq"}) == 7'100'000);
}
