#include "pending_writes.h"

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

// Drop every pending entry without persisting it.
void PendingWrites::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ints_.clear();
    pending_floats_.clear();
    pending_texts_.clear();
}

// Persist all pending changes. Values are written via store_save(), which
// routes each key to the logical table named by its StorageType; only the last
// value per key is kept, so repeated flushes are idempotent. Each entry is
// erased only when its save succeeds (retained for retry otherwise), so a
// single pass is sufficient.
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
            int rc = store_save<MappedT>(key.type, key.context_id, key.name.c_str(), it->second);

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
