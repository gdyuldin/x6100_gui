#include "pending_writes.h"
#include <optional>

class PendingWritesTestAccess {
  public:
    static std::optional<int32_t> peek_int(const PendingWrites &pending, const StorageKey &key) {
        std::lock_guard<std::mutex> lock(pending.mutex_);
        auto                        it = pending.pending_ints_.find(key);
        if (it != pending.pending_ints_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    static std::optional<float> peek_float(const PendingWrites &pending, const StorageKey &key) {
        std::lock_guard<std::mutex> lock(pending.mutex_);
        auto                        it = pending.pending_floats_.find(key);
        if (it != pending.pending_floats_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    static std::optional<std::string> peek_text(const PendingWrites &pending, const StorageKey &key) {
        std::lock_guard<std::mutex> lock(pending.mutex_);
        auto                        it = pending.pending_texts_.find(key);
        if (it != pending.pending_texts_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
};
