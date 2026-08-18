#pragma once

#include <cstdint>
#include <vector>

#include "db.h"
#include "subject.h"

// Reactive cache for the per-antenna, per-frequency ATU tuner-network values.
// Port of the legacy src/cfg/atu.c logic. It loads all saved networks for the
// active antenna from AtuTable, finds the nearest network within
// ATU_SAVE_STEP Hz of the current frequency, and publishes the result via two
// public subjects (`loaded` bool, `network` value). It is pure logic: it never
// touches LVGL or the UI.
//
// Not part of the Parameter<T>/StoragePolicy/PendingWrites pipeline (composite
// key, bulk load, nearest-match, immediate save). Subscriptions to ant_id,
// fg_freq and atu_enabled are NOT wired here — the owner wires them externally
// (see cfg_api.cpp) and calls on_params_changed.
class AtuNetworkCache {
  public:
    AtuNetworkCache();
    ~AtuNetworkCache() = default;

    AtuNetworkCache(const AtuNetworkCache &)            = delete;
    AtuNetworkCache &operator=(const AtuNetworkCache &) = delete;
    AtuNetworkCache(AtuNetworkCache &&)                 = delete;
    AtuNetworkCache &operator=(AtuNetworkCache &&)      = delete;

    // Public subjects (read by the C API / UI / radio code).
    SubjectT<bool>     loaded{false};
    SubjectT<uint32_t> network{0};

    // Called when frequency, antenna, or ATU-enabled changes.
    // If atu_enabled is false, publishes loaded=false, network=0 and returns.
    // If antenna changed, reloads the cache from AtuTable.
    // Finds the nearest cached network within ATU_SAVE_STEP Hz and publishes.
    void on_params_changed(int32_t ant_id, int32_t freq, bool atu_enabled);

    // Save a freshly-tuned network immediately (called from radio.c on tune
    // completion). Writes to the DB, deletes adjacent entries within
    // ATU_SAVE_STEP, refreshes the cache, and publishes loaded=true /
    // network=value. Returns SUCCESS or a sqlite3 rc.
    int save_network(int32_t ant_id, int32_t freq, uint32_t network);

  private:
    static constexpr int32_t ATU_SAVE_STEP = 25000;

    int32_t                         current_ant_id_ = -1;
    std::vector<AtuTable::AtuEntry> cache_;

    void load_cache(int32_t ant_id);
    int  find_nearest(int32_t freq) const;         // returns index into cache_ or -1
    void publish(const AtuTable::AtuEntry *entry); // sets loaded/network
};
