#include "settings_manager_api.h"

#include "db.h"
#include "settings_manager.h"

// SettingsManager-level operations exposed to C. cfg_api_init lives in
// cfg_api.cpp (it also fills the extern parameter pointers); these are the
// pure manager operations that mostly touch only cfg_sm.

int32_t cfg_transverter_shift_for(int32_t freq) {
    return cfg_sm.transverter_shift_for(freq);
}

bool cfg_is_valid_hw_freq(int32_t freq) {
    return cfg_sm.is_valid_hw_freq(freq);
}

void cfg_api_flush_all(void) {
    cfg_sm.flush_all();
}

void cfg_api_start_flush_thread(void) {
    cfg_sm.start_flush_thread();
}

void cfg_api_stop_flush_thread(void) {
    cfg_sm.stop_flush_thread();
}

void cfg_band_load_next(bool up) {
    int32_t            cur_freq = cfg_sm.cp_fg_freq.get();
    int32_t            cur_id   = cfg_sm.current_band_id();
    BandInfoLoadResult result   = BandsTable::next(cur_id, static_cast<uint32_t>(cur_freq), up);
    if (result.rc == SUCCESS) {
        cfg_sm.p_band_id.set(result.value.id);
    }
}

void cfg_band_vfo_copy(void) {
    cfg_sm.cfg_band_vfo_copy();
}

int32_t cfg_mode_change_freq_step(bool up) {
    static const uint16_t steps[] = {10, 100, 500, 1000, 5000};
    static const size_t   n       = sizeof(steps) / sizeof(steps[0]);
    int32_t               step    = cfg_sm.p_mode_freq_step.get();
    size_t                i;
    for (i = 0; i < n; i++)
        if (step == static_cast<int32_t>(steps[i]))
            break;
    i    = (i + (up ? 1 : -1) + n) % n;
    step = steps[i];
    cfg_sm.p_mode_freq_step.set(step);
    return step;
}