#pragma once

// Internal cfg access to the SettingsManager singleton.
//
// This header is for cfg-internal code only (cfg_api.cpp and the other cfg
// translation units). Consumers outside src/cfg must use the C API facade in
// cfg_api.h (cfg.<group>.<name>() / param_i_get / cparam_i_set) and never
// include this header.

#include "settings_manager.h"

// Global SettingsManager instance, defined in cfg_api.cpp and owned by C++ for
// the whole program. Never deleted.
extern SettingsManager cfg_sm;

// Active instance used by the cfg_api.h handle accessors. Defaults to cfg_sm;
// tests may point it at their own SettingsManager via cfg_set_instance().
SettingsManager &cfg_instance();

// Test seam: redirect the accessors to a caller-owned manager. Pass nullptr to
// restore the global cfg_sm. The caller must keep the manager alive while it is
// installed and reset it afterwards.
void cfg_set_instance(SettingsManager *sm);

// Wire the ATU cache's public subjects to the parameter sources (p_ant_id,
// cp_fg_freq, p_atu_enabled) and do the initial load. Defined in cfg_api.cpp,
// called once from cfg_api_init(). Internal: not part of the C API.
void atu_wire_subscriptions(void);
