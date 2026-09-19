#pragma once

#include "parameter.h"

#ifdef __cplusplus

#include <mutex>
#include <string>
#include <unordered_map>

// StorageType is defined in db.h; StorageKey/StorageKeyHash — in parameter.h.

class PendingWrites;

// Deferred-write buffer: stores only the last value per key. Parameter::set()
// enqueues the new value; a background thread (or switch_band/flush) persists
// them via flush_all()/flush_storage().
//
// Concurrency invariants (I1/I2) — do not break:
//   I1 (isolation by construction): each entry is a SNAPSHOT
//     (StorageType, context_id, name + value) with no reference to any external
//     mutable parameter. The flush thread only ever reads the queue's own
//     snapshot entries and the stateless store_save() routing helper; it never
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
    PendingWrites() = default;

    void write(const StorageKey &key, int32_t value) override;
    void write(const StorageKey &key, float value) override;
    void write(const StorageKey &key, const std::string &value) override;

    // Persist all pending changes through store_save().
    void flush_all();

    // Persist pending changes for one logical table (type + context_id).
    // For the flat GLOBAL table context_id is ignored; band/mode tables use it.
    void flush_storage(StorageType type, int context_id);

    // Drop every pending entry without saving. Used by
    // SettingsManager::init_load's reset before reloading from the DB.
    void clear();

  private:
    friend class PendingWritesTestAccess;

    std::unordered_map<StorageKey, int32_t, StorageKeyHash>     pending_ints_;
    std::unordered_map<StorageKey, float, StorageKeyHash>       pending_floats_;
    std::unordered_map<StorageKey, std::string, StorageKeyHash> pending_texts_;
    mutable std::mutex                                          mutex_;
};

#endif
