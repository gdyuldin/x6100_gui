#include "atu_cache.h"

#include <cstdlib>

#include "../lvgl/lvgl.h"

AtuNetworkCache::AtuNetworkCache() = default;

void AtuNetworkCache::load_cache(int32_t ant_id) {
    cache_.clear();
    int rc = AtuTable::LoadAll(ant_id, cache_);
    if (rc != SUCCESS) {
        LV_LOG_ERROR("Failed to load ATU cache for ant %i: %i", ant_id, rc);
        cache_.clear();
        return;
    }
    LV_LOG_INFO("Loaded %zu ATU networks for ant %i", cache_.size(), ant_id);
}

int AtuNetworkCache::find_nearest(int32_t freq) const {
    int32_t min_diff = ATU_SAVE_STEP + 1;
    int32_t min_pos  = -1;
    for (size_t i = 0; i < cache_.size(); i++) {
        int32_t diff = std::abs(cache_[i].freq - freq);
        if (diff < min_diff) {
            min_diff = diff;
            min_pos  = static_cast<int32_t>(i);
        }
    }
    if (min_diff <= ATU_SAVE_STEP) {
        return min_pos;
    }
    return -1;
}

void AtuNetworkCache::publish(const AtuTable::AtuEntry *entry) {
    if (entry) {
        loaded.set(true);
        network.set(entry->network);
        LV_LOG_INFO("Loaded ATU network for ant: %i - %u", current_ant_id_, entry->network);
    } else {
        loaded.set(false);
        network.set(0);
        LV_LOG_INFO("ATU network for ant: %i not found", current_ant_id_);
    }
}

void AtuNetworkCache::on_params_changed(int32_t ant_id, int32_t freq, bool atu_enabled) {
    if (!atu_enabled) {
        publish(nullptr);
        return;
    }
    if (current_ant_id_ != ant_id) {
        current_ant_id_ = ant_id;
        load_cache(ant_id);
    }
    int min_pos = find_nearest(freq);
    if (min_pos >= 0) {
        publish(&cache_[min_pos]);
    } else {
        publish(nullptr);
    }
}

int AtuNetworkCache::save_network(int32_t ant_id, int32_t freq, uint32_t network) {
    LV_LOG_INFO("Saving ATU network %u for freq: %i and ant: %i", network, freq, ant_id);

    int rc = AtuTable::Save(ant_id, freq, network);
    if (rc != SUCCESS) {
        LV_LOG_ERROR("Failed to save ATU network: %i", rc);
        return rc;
    }
    rc = AtuTable::DeleteAdjacent(ant_id, freq, ATU_SAVE_STEP);
    if (rc != SUCCESS) {
        LV_LOG_ERROR("Failed to delete adjacent ATU networks: %i", rc);
        return rc;
    }

    current_ant_id_ = ant_id;
    load_cache(ant_id);
    loaded.set(true);
    this->network.set(network);
    return SUCCESS;
}
