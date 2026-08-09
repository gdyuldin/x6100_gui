// digital_modes.cpp
// Implementation of the C API for digital mode (FT8/FT4) preset navigation.
//
// This is the C++/C API boundary: it reads the current front-panel frequency
// from the global SettingsManager (g_cfg), queries the preset through
// DigitalModesTable (db.h/db.cpp) and writes the found frequency/mode back
// through the computed params cp_fg_freq / cp_cur_mode (their reverse paths
// write into the active VFO band params and trigger mode/band switching).

#include "digital_modes.h"

#include <string>

#include "db.h"
#include "settings_manager.h"

namespace {

// Label of the last successfully loaded preset; retained only until the next
// successful load (matches the legacy C module's strdup'ed label).
std::string last_label;

} // namespace

extern "C" bool app_cfg_digital_load(int8_t dir, app_cfg_digital_type_t type) {
    int32_t cur_freq = g_cfg.cp_fg_freq.get();

    DigitalModesTable::LoadResult result;
    if (dir > 0) {
        result = DigitalModesTable::find_next(type, cur_freq);
    } else if (dir == 0) {
        result = DigitalModesTable::find_closest(type, cur_freq);
    } else {
        result = DigitalModesTable::find_prev(type, cur_freq);
    }
    if (result.rc != SUCCESS) {
        return false;
    }

    last_label = result.value.label;
    g_cfg.cp_fg_freq.set(result.value.freq);
    g_cfg.cp_cur_mode.set(result.value.mode);
    return true;
}

extern "C" const char *app_cfg_digital_label_get(void) {
    return last_label.c_str();
}