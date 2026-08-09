// memory.cpp
// Orchestration layer for user memory slots on the `memory` table.
//
// This is the C++/C API boundary: it reads/writes the current active VFO
// settings from the global SettingsManager (g_cfg) via its computed params
// and persists them through MemoryTable (db.h/db.cpp). It does not use the
// deferred-write path: memory saves are immediate and loads are on demand.
//
// Field names match the legacy src/cfg module (vfoa_freq, vfoa_mode, vfoa_agc,
// vfoa_pre, vfoa_att) so existing user memory configurations remain readable.

#include "memory.h"

#include "db.h"
#include "settings_manager.h"
#include "subject.h"

// cfg_memory_save: store the current active VFO frequency, mode, agc, pre and
// att into the memory slot `id`. Returns true when every field was saved.
extern "C" bool cfg_memory_save(int32_t id) {
    bool ok = true;
    ok      = MemoryTable::Save(id, "vfoa_freq", g_cfg.cp_fg_freq.get()) == SUCCESS && ok;
    ok      = MemoryTable::Save(id, "vfoa_mode", g_cfg.cp_cur_mode.get()) == SUCCESS && ok;
    ok      = MemoryTable::Save(id, "vfoa_agc", g_cfg.cp_cur_agc.get()) == SUCCESS && ok;
    ok      = MemoryTable::Save(id, "vfoa_pre", g_cfg.cp_cur_pre.get()) == SUCCESS && ok;
    ok      = MemoryTable::Save(id, "vfoa_att", g_cfg.cp_cur_att.get()) == SUCCESS && ok;
    return ok;
}

// cfg_memory_load: restore the memory slot `id`. The active VFO frequency is
// required; without it the slot is not loadable. The band (derived from the
// stored frequency) is set first so the band context is correct before the
// frequency observer fires.
extern "C" bool cfg_memory_load(int32_t id) {
    int32_t freq = 0, mode = 0, agc = 0, att = 0, pre = 0;
    bool    has_freq = false, has_mode = false, has_agc = false, has_att = false, has_pre = false;

    if (!MemoryTable::Load(id, freq, has_freq, mode, has_mode, agc, has_agc, att, has_att, pre, has_pre)) {
        return false;
    }

    // Derive the band from the restored frequency (mirrors legacy behaviour:
    // a frequency in no active band maps to BAND_UNDEFINED).
    int32_t            band_id = BAND_UNDEFINED;
    BandInfoLoadResult band    = BandsTable::get_by_freq(static_cast<uint32_t>(freq));
    if (band.rc == SUCCESS && band.value.id != BAND_UNDEFINED) {
        band_id = band.value.id;
    }

    // Set the band before the frequency so the implicit band switch (triggered
    // by the freq observers) lands in the correct band context. This write is
    // NOT suppressed: its observer triggers switch_band(), which batches its
    // own notification round internally.
    g_cfg.p_band_id.set(band_id);

    // Batch the frequency/mode/agc/pre/att restore into a single notification
    // round. Each computed set() reverse-writes into the source VFO params,
    // which would otherwise cascade into one notify per intermediate state.
    {
        NotifySuppressGuard guard;

        g_cfg.cp_fg_freq.set(freq);

        if (has_mode) {
            g_cfg.cp_cur_mode.set(mode);
        }
        if (has_agc) {
            g_cfg.cp_cur_agc.set(agc);
        }
        if (has_pre) {
            g_cfg.cp_cur_pre.set(pre);
        }
        if (has_att) {
            g_cfg.cp_cur_att.set(att);
        }
    }
    return true;
}
