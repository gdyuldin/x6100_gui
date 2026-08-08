#pragma once

// In-memory mock of StoragePolicy for tests. Implements the full virtual
// interface of StoragePolicy (storage_policy.h) without touching SQLite, so
// PendingWrites flush paths can be tested in isolation. Written by hand, used
// only by the test binary (never compiled into app_cfg).
//
// The mock keeps three maps (int32_t / float / std::string) keyed by
// StorageKey and records the number of successful save_* calls so tests can
// assert that a flush actually persisted values.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "storage_policy.h"

// In-memory mock of StoragePolicy for tests. Implements the full virtual
// interface of StoragePolicy (storage_policy.h) without touching SQLite, so
// PendingWrites flush paths can be tested in isolation. Written by hand, used
// only by the test binary (never compiled into app_cfg).
//
// Each instance is bound to one StorageType (passed to the ctor) and stores
// values under that type, mirroring the production design where each logical
// table has its own policy instance (ModeStorage only ever targets
// mode_params). The mock records the number of successful save_* calls so
// tests can assert that a flush actually persisted values.
class MockStorage : public StoragePolicy {
public:
    explicit MockStorage(StorageType type) : type_(type) {}

    // StoragePolicy interface ------------------------------------------------
    // If fail_next_save_ is set, the next save_* call returns the configured
    // error code without persisting, then resets the flag.
    int save_int(int context_id, const char* name, int32_t value) override
    {
        if (fail_next_save_) {
            fail_next_save_ = false;
            return fail_rc_;
        }
        ints_[{type_, context_id, name}] = value;
        saved_int_count_++;
        return 0;
    }

    std::optional<int32_t> load_int(int context_id, const char* name) override
    {
        auto it = ints_.find({type_, context_id, name});
        if (it == ints_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    int save_float(int context_id, const char* name, float value) override
    {
        if (fail_next_save_) {
            fail_next_save_ = false;
            return fail_rc_;
        }
        floats_[{type_, context_id, name}] = value;
        saved_float_count_++;
        return 0;
    }

    std::optional<float> load_float(int context_id, const char* name) override
    {
        auto it = floats_.find({type_, context_id, name});
        if (it == floats_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    int save_text(int context_id, const char* name, const std::string& value) override
    {
        if (fail_next_save_) {
            fail_next_save_ = false;
            return fail_rc_;
        }
        texts_[{type_, context_id, name}] = value;
        saved_text_count_++;
        return 0;
    }

    std::optional<std::string> load_text(int context_id, const char* name) override
    {
        auto it = texts_.find({type_, context_id, name});
        if (it == texts_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Test helpers -----------------------------------------------------------
    void clear()
    {
        ints_.clear();
        floats_.clear();
        texts_.clear();
        saved_int_count_    = 0;
        saved_float_count_  = 0;
        saved_text_count_   = 0;
        fail_next_save_     = false;
    }

    // Arm the next single save_* call to fail with the given rc.
    void arm_fail_save(int rc) { fail_next_save_ = true; fail_rc_ = rc; }

    const std::unordered_map<StorageKey, int32_t, StorageKeyHash>& ints() const { return ints_; }
    const std::unordered_map<StorageKey, float, StorageKeyHash>&   floats() const { return floats_; }
    const std::unordered_map<StorageKey, std::string, StorageKeyHash>& texts() const { return texts_; }

    int saved_int_count() const   { return saved_int_count_; }
    int saved_float_count() const { return saved_float_count_; }
    int saved_text_count() const  { return saved_text_count_; }

private:
    // The logical table this instance emulates; every stored key is tagged
    // with it.
    StorageType type_;

    std::unordered_map<StorageKey, int32_t, StorageKeyHash>     ints_;
    std::unordered_map<StorageKey, float, StorageKeyHash>       floats_;
    std::unordered_map<StorageKey, std::string, StorageKeyHash> texts_;

    int saved_int_count_   = 0;
    int saved_float_count_ = 0;
    int saved_text_count_  = 0;

    bool fail_next_save_ = false;
    int  fail_rc_        = 0;
};
