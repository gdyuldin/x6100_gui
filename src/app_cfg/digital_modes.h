#pragma once

// C-compatible API for digital mode (FT8/FT4) preset navigation. Replaces the
// legacy src/cfg/digital_modes.h: the functions read the current front-panel
// frequency from the global SettingsManager (cp_fg_freq), query the
// `digital_modes` table and write the found frequency/mode back through the
// computed params (cp_fg_freq / cp_cur_mode).

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol type as stored in the digital_modes.type column (0 = FT8, 1 = FT4).
// Values are part of the DB format and must not be changed.
typedef enum {
    APP_CFG_DIG_TYPE_FT8 = 0,
    APP_CFG_DIG_TYPE_FT4 = 1,
} app_cfg_digital_type_t;

// Load the next (dir > 0), closest (dir == 0) or previous (dir < 0) digital
// mode preset for `type` relative to the current front-panel frequency, and
// apply it (frequency + mode). Returns true on success; on NOT_FOUND or a DB
// error returns false and leaves the frequency, mode and label unchanged.
bool app_cfg_digital_load(int8_t dir, app_cfg_digital_type_t type);

// Label of the last successfully loaded preset. Borrowed pointer: valid until
// the next successful app_cfg_digital_load() call; empty string before any
// successful load.
const char *app_cfg_digital_label_get(void);

#ifdef __cplusplus
}
#endif