#include "pending_writes.h"
#include "storage_policy.h"

#include <cassert>
#include <type_traits>

void PendingWrites::write(const StorageKey &key, int32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ints_[key] = value;
}

void PendingWrites::write(const StorageKey &key, float value) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_floats_[key] = value;
}

void PendingWrites::write(const StorageKey &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_texts_[key] = value;
}

// Select the storage policy to use for a logical table. Production builds use
// the stateless GlobalStorage/BandStorage/ModeStorage via storage_policy_for();
// tests may inject a per-type resolver through the constructor (which returns
// e.g. a different mock per table, see tests/cfg/mocks/mock_storage.h).
StoragePolicy &PendingWrites::policy_for(StorageType type) {
    if (policy_resolver_) {
        return policy_resolver_(type);
    }

    return storage_policy_for(type);
}

// Persist all pending changes. Values are written via the StoragePolicy that
// matches the logical table of each key; only the last value per key is kept,
// so repeated flushes are idempotent. Each entry is erased only when its save
// succeeds (retained for retry otherwise), so a single pass is sufficient.
void PendingWrites::flush_all() {
    flush_storage(StorageType::GLOBAL, -1);
    flush_storage(StorageType::BAND, -1);
    flush_storage(StorageType::MODE, -1);
    flush_storage(StorageType::TRANSVERTER, -1);
}

// Persist pending changes belonging to one logical table and erase them. For
// the flat GLOBAL table the context_id passed here is ignored (keys are stored
// with context_id 0); band/mode tables filter by context_id. A context_id of
// -1 matches every entry of that type.
void PendingWrites::flush_storage(StorageType type, int context_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto flush_fn = [&](auto &map) {
        using MapT    = std::remove_reference_t<decltype(map)>;
        using MappedT = typename MapT::mapped_type;

        for (auto it = map.begin(); it != map.end();) {
            const StorageKey &key = it->first;
            if (key.type != type) {
                ++it;
                continue;
            }
            if (context_id != -1 && key.context_id != context_id) {
                ++it;
                continue;
            }

            // Only drop the pending entry when the save actually succeeded.
            // On failure (positive sqlite rc / negative error code) keep the
            // entry so a later flush can retry it; the value is never silently
            // lost.
            StoragePolicy &policy = policy_for(key.type);
            int            rc     = SUCCESS;
            if constexpr (std::is_same_v<MappedT, int32_t>) {
                rc = policy.save_int(key.context_id, key.name.c_str(), it->second);
            } else if constexpr (std::is_same_v<MappedT, float>) {
                rc = policy.save_float(key.context_id, key.name.c_str(), it->second);
            } else if constexpr (std::is_same_v<MappedT, std::string>) {
                rc = policy.save_text(key.context_id, key.name.c_str(), it->second);
            }

            if (rc == SUCCESS) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };

    flush_fn(pending_ints_);
    flush_fn(pending_floats_);
    flush_fn(pending_texts_);
}
