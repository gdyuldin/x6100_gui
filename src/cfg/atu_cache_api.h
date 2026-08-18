#pragma once

// C-compatible API for the ATU tuner-network cache (AtuNetworkCache in
// atu_cache.h). The cache lives here as a C++ static, exposed to C code as
// subscribe/read helpers; saving a freshly-tuned network wakes the cache.

#include <stdbool.h>
#include <stdint.h>

#include "subject_api.h" // Observer / ObserverDelayed / observer_cb

#ifdef __cplusplus
// Internal (C++ only): wires the cache's public subjects to the parameter
// sources (p_ant_id, cp_fg_freq, p_atu_enabled) and does the initial load.
// Called once from cfg_api_init(). Not part of the C API surface.
void atu_cache_wire_subscriptions(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Save a freshly-tuned network immediately for the current antenna/frequency
// (replacement for the legacy cfg_atu_save_network). Values are the auto-tuner
// network number for the current front-panel frequency.
int cfg_atu_save_network(uint32_t network);

// Direct value reads (for C code that needs the current value without
// subscribing).
bool     cfg_atu_is_loaded(void);
uint32_t cfg_atu_get_network(void);

// ATU subjects: whether a saved network exists for the current freq/ant and
// its value (0 when not loaded). The returned Observer / ObserverDelayed is
// freed with param_unsubscribe.
Observer        *cfg_atu_loaded_subscribe(observer_cb cb, void *user_data);
Observer        *cfg_atu_network_subscribe(observer_cb cb, void *user_data);
ObserverDelayed *cfg_atu_loaded_subscribe_delayed(observer_cb cb, void *user_data);
ObserverDelayed *cfg_atu_network_subscribe_delayed(observer_cb cb, void *user_data);

#ifdef __cplusplus
} // extern "C"
#endif