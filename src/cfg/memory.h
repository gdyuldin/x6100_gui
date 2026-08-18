#pragma once

// C/C++ API for user memory slots on the `memory` table.
//
// A memory slot is a named KV snapshot identified by an integer id. Saving
// stores the current active VFO settings (frequency, mode, agc, pre, att);
// loading restores them. IDs 1..9 are reserved for the hardware-key user
// memories and 10 (MEM_BACKUP_ID) is the scratch backup slot used by dialogs,
// matching the legacy src/cfg module. Field names are preserved exactly
// (vfoa_freq, vfoa_mode, vfoa_agc, vfoa_pre, vfoa_att) so configurations saved
// by the old module remain readable.
//
// These functions read/write the global SettingsManager state (cfg_sm), so the
// DB and the manager must be initialised first (cfg_db_init + cfg_api_init).

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Store the current active VFO frequency, mode, agc, pre and att into the
// memory slot `id`. Returns true on success, false if any field failed to save.
bool cfg_memory_save(int32_t id);

// Restore the memory slot `id`. Returns false (and leaves the radio settings
// untouched) when the slot has no stored vfoa_freq row.
bool cfg_memory_load(int32_t id);

#ifdef __cplusplus
} // extern "C"
#endif
