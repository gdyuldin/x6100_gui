#pragma once

// C-compatible API for the SettingsManager-level operations (settings_manager.h):
// initialisation, deferred-write flushing, band/VFO switching, frequency-step
// cycling and hardware-frequency helpers. Parameter value accessors are in
// parameter_api.h.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the manager (loads global/band/mode params) and fill the extern
// globals in cfg_api.h. The caller owns the sqlite3 connection/table init
// (cfg_api_init is not handed a db handle). on_db_error is kept for signature
// compatibility; it is currently inert.
void cfg_api_init(void (*on_db_error)(const char *));

// Transverter shift for a frequency: the shift of the transverter whose
// [from, to] range contains freq, or 0 when none covers it.
int32_t cfg_transverter_shift_for(int32_t freq);

// True when freq is usable by the hardware: HF 0.5-55 MHz or inside any
// transverter range.
bool cfg_is_valid_hw_freq(int32_t freq);

// Persist all pending deferred writes immediately.
void cfg_api_flush_all(void);

// Start/stop the background deferred-save thread; wakes every ~3 s and calls
// flush_all(). Inert/no-op when the thread is already running / already stopped.
void cfg_api_start_flush_thread(void);
void cfg_api_stop_flush_thread(void);

// --- Band switching ---
// Load the next/previous band above/below current frequency.
void cfg_band_load_next(bool up);

// Copy active VFO (freq, mode, agc, att, pre) to inactive VFO.
void cfg_band_vfo_copy(void);

// --- Step cycling ---
// Cycle freq_step through [10, 100, 500, 1000, 5000] Hz. Returns new step.
int32_t cfg_mode_change_freq_step(bool up);

#ifdef __cplusplus
} // extern "C"
#endif