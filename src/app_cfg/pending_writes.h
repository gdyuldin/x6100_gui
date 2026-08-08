#pragma once

#include "parameter.h"

#ifdef __cplusplus

#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

// StorageKey, StorageKeyHash and StorageType are defined in parameter.h.

class StoragePolicy;            // defined in storage_policy.h
class PendingWrites;

// Deferred-write buffer: stores only the last value per key. Parameter::set()
// enqueues the new value; a background thread (or switch_band/flush) persists
// them via flush_all()/flush_storage().
//
// Concurrency invariants (I1/I2) — do not break:
//   I1 (isolation by construction): each entry is a SNAPSHOT
//     (StorageType, context_id, name + value) with no reference to any external
//     mutable parameter. The flush thread only ever reads the queue's own
//     snapshot entries and the stateless storage policies; it never
//     dereferences or mutates SettingsManager state. If a future change makes
//     any flush path touch a manager member directly, that is a data race.
//   I2 (strict lock order): whenever the queue is involved, its mutex_ is
//     acquired FIRST, then a statement mutex via StmtResetGuard
//     (queue.mutex_ -> statement.mutex_). Never acquire the queue mutex while
//     already holding a statement mutex elsewhere, or a latent AB-BA deadlock
//     (two statements, opposite order) becomes possible. No lock cycle exists
//     today.
class PendingWrites : public WriteSink {
public:
    // Default: route flushes to the stateless production policies via
    // storage_policy_for(). Tests pass a per-type resolver so each logical
    // table can be backed by its own mock (one table = one class), matching
    // the production design where e.g. ModeStorage only ever targets
    // mode_params.
    PendingWrites() = default;

    // resolver maps a StorageType to the StoragePolicy that should persist it.
    // Non-owning: the caller (test) owns the returned policies.
    explicit PendingWrites(std::function<StoragePolicy&(StorageType)> resolver)
        : policy_resolver_(std::move(resolver))
    {
    }

    void write(const StorageKey& key, int32_t value) override;
    void write(const StorageKey& key, float value) override;
    void write(const StorageKey& key, const std::string& value) override;

    // Persist all pending changes through the StoragePolicy.
    void flush_all();

    // Persist pending changes for one logical table (type + context_id).
    // For the flat GLOBAL table context_id is ignored; band/mode tables use it.
    void flush_storage(StorageType type, int context_id);

private:
    friend class PendingWritesTestAccess;

    StoragePolicy& policy_for(StorageType type);

    // Optional per-type resolver. Empty (default) selects the stateless
    // production policies via storage_policy_for(); tests use it to inject
    // per-table mocks.
    std::function<StoragePolicy&(StorageType)> policy_resolver_;

    std::unordered_map<StorageKey, int32_t, StorageKeyHash>     pending_ints_;
    std::unordered_map<StorageKey, float, StorageKeyHash>       pending_floats_;
    std::unordered_map<StorageKey, std::string, StorageKeyHash> pending_texts_;
    mutable std::mutex mutex_;
};

#endif
