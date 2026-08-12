#include "atu_cache_api.h"

#include "atu_cache.h"
#include "settings_manager.h"
#include "subject.h"

// The ATU network cache singleton, owned by C++ for the whole program; wired to
// p_ant_id / cp_fg_freq / p_atu_enabled in atu_cache_wire_subscriptions.
static AtuNetworkCache g_atu_cache;

void atu_cache_wire_subscriptions(void) {
    // Recompute the published loaded/network subjects whenever the antenna,
    // front-panel frequency, or ATU-enabled changes.
    auto on_atu_param_change = [](Subject * /*subj*/, void * /*user_data*/) -> void {
        g_atu_cache.on_params_changed(cfg_sm.p_ant_id.get(), cfg_sm.cp_fg_freq.get(), cfg_sm.p_atu_enabled.get() != 0);
    };
    // Keep the subscriptions alive for the whole program (never unsubscribed).
    static Subscription atu_ant_obs(cfg_sm.p_ant_id.subscribe(on_atu_param_change, nullptr));
    static Subscription atu_freq_obs(cfg_sm.cp_fg_freq.subscribe(on_atu_param_change, nullptr));
    static Subscription atu_enabled_obs(cfg_sm.p_atu_enabled.subscribe(on_atu_param_change, nullptr));

    // Initial load: populate the cache for the current antenna/frequency.
    g_atu_cache.on_params_changed(cfg_sm.p_ant_id.get(), cfg_sm.cp_fg_freq.get(), cfg_sm.p_atu_enabled.get() != 0);
}

int cfg_atu_save_network(uint32_t network) {
    return g_atu_cache.save_network(cfg_sm.p_ant_id.get(), cfg_sm.cp_fg_freq.get(), network);
}

bool cfg_atu_is_loaded(void) {
    return g_atu_cache.loaded.get();
}

uint32_t cfg_atu_get_network(void) {
    return g_atu_cache.network.get();
}

Observer *cfg_atu_loaded_subscribe(observer_cb cb, void *user_data) {
    return g_atu_cache.loaded.subscribe(cb, user_data);
}

ObserverDelayed *cfg_atu_loaded_subscribe_delayed(observer_cb cb, void *user_data) {
    return g_atu_cache.loaded.subscribe_delayed(cb, user_data);
}

Observer *cfg_atu_network_subscribe(observer_cb cb, void *user_data) {
    return g_atu_cache.network.subscribe(cb, user_data);
}

ObserverDelayed *cfg_atu_network_subscribe_delayed(observer_cb cb, void *user_data) {
    return g_atu_cache.network.subscribe_delayed(cb, user_data);
}