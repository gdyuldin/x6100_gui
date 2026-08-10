#pragma once

// C-compatible API for the SettingsManager's parameters (Stage B). Exposes the
// manager's typed parameters and the computed front-panel frequency to C code
// as opaque handles. Typed functions only (no void* in the public surface).
//
// All accessors route through Parameter<T>::set / SubjectT<T>::get, so
// validators, deferred-write enqueue and observer notifications all apply.
//
// Ownership: the parameters (and the SettingsManager singleton) are static and
// owned by C++ (cfg_api.cpp). C code only receives/holds opaque pointers and
// must never free them. Observers returned by param_*_subscribe are owned by
// C/UI code and freed with param_unsubscribe.
//
// cfg_api_init() does NOT open the DB or call cfg_db_init(): the caller owns
// the sqlite3 connection and table initialisation (avoids double-Init).

#include <stdint.h>

#include "computed_api.h"

#ifdef __cplusplus
#include "subject.h" // real (namespaced) Observer / ObserverDelayed types

// C++ build: re-expose the namespaced observer types under the plain names so
// the extern "C" prototypes below stay valid for both languages. The aliases
// are visible only to TUs that include this header; no such TU also includes
// the legacy cfg/subjects.h, so there is no conflict during the transition.
using Observer = appcfg::Observer;
using ObserverDelayed = appcfg::ObserverDelayed;
#else
// Opaque C handles for the C++ observer types (resolved to the real ones in
// C++ builds).
typedef struct Observer Observer;
typedef struct ObserverDelayed ObserverDelayed;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ParamInt   ParamInt;   // = Parameter<int32_t>
typedef struct ParamFloat ParamFloat; // = Parameter<float>
typedef struct ParamText  ParamText;  // = Parameter<std::string>

int32_t     param_int_get(const ParamInt* p);
void        param_int_set(ParamInt* p, int32_t v);
float       param_float_get(const ParamFloat* p);
void        param_float_set(ParamFloat* p, float v);
const char* param_text_get(const ParamText* p);             // borrowed: valid until next set
void        param_text_set(ParamText* p, const char* v);    // NULL -> ""

typedef void (*param_int_cb)(ParamInt* p, void* user_data);
typedef void (*param_float_cb)(ParamFloat* p, void* user_data);
typedef void (*param_text_cb)(ParamText* p, void* user_data);

// Subscribe to a parameter. Returns the internal Observer* / ObserverDelayed*
// (appcfg::Observer in C++) that must be released later with param_unsubscribe.
Observer*        param_int_subscribe(ParamInt* p, param_int_cb cb, void* user_data);
ObserverDelayed* param_int_subscribe_delayed(ParamInt* p, param_int_cb cb, void* user_data);
Observer*        param_float_subscribe(ParamFloat* p, param_float_cb cb, void* user_data);
ObserverDelayed* param_float_subscribe_delayed(ParamFloat* p, param_float_cb cb, void* user_data);
Observer*        param_text_subscribe(ParamText* p, param_text_cb cb, void* user_data);
ObserverDelayed* param_text_subscribe_delayed(ParamText* p, param_text_cb cb, void* user_data);

// Unsubscribe + destroy (ObserverDeleter) + free the per-subscription adapter.
void param_unsubscribe(Observer* o);

// --- Opaque extern globals, filled by cfg_api_init() ---
// Each maps to a concrete public SettingsManager member. All params are
// int32_t today; no text parameter exists in the manager yet (the ParamText
// API surface is provided for completeness but no global is wired).
// The extern list is the exhaustive set of C-reachable parameters — not every
// SettingsManager parameter is exposed to C (VFO mode params, encoder_bind).
extern ParamInt*   cfg_volume;
extern ParamInt*   cfg_squelch;
extern ParamInt*   cfg_rfgain;
extern ParamInt*   cfg_rit;
extern ParamInt*   cfg_xit;
extern ParamFloat* cfg_pwr;
extern ParamInt*   cfg_band_current_vfo;
extern ParamInt*   cfg_band_if_shift;
extern ParamInt*   cfg_mode_squelch;
extern ParamInt*   cfg_mode_agc;
extern ParamInt*           cfg_band_vfoa_freq; // p_band_vfoa_freq (BAND)
extern ParamInt*           cfg_band_vfob_freq; // p_band_vfob_freq (BAND)
extern ParamFloat*         cfg_band_dac_offset; // p_band_dac_offse (BAND)
extern ComputedParamInt*   cfg_fg_freq;       // cp_fg_freq

// Transverter params (p_transverter_{0,1}_{from,to,shift}). Values are Hz.
extern ParamInt* cfg_transverter_0_from;
extern ParamInt* cfg_transverter_0_to;
extern ParamInt* cfg_transverter_0_shift;
extern ParamInt* cfg_transverter_1_from;
extern ParamInt* cfg_transverter_1_to;
extern ParamInt* cfg_transverter_1_shift;

// Transverter shift for a frequency: the shift of the transverter whose
// [from, to] range contains freq, or 0 when none covers it (g_cfg.transverter_shift_for).
int32_t cfg_transverter_shift_for(int32_t freq);

// True when freq is usable by the hardware: HF 0.5-55 MHz or inside any
// transverter range (g_cfg.is_valid_hw_freq).
bool cfg_is_valid_hw_freq(int32_t freq);

// Initialise the manager (loads global/band/mode params; the starting band comes
// from the persisted global band_id and the starting mode from cp_cur_mode, so
// no band/mode ids are passed) and fill the extern globals above. The caller
// owns the sqlite3 connection/table init (cfg_api_init is not handed a db
// handle). on_db_error is kept for signature compatibility; it is currently
// inert (see plan A.1).
void cfg_api_init(void (*on_db_error)(const char*));

// Persist all pending deferred writes immediately.
void cfg_api_flush_all(void);

#ifdef __cplusplus
} // extern "C"
#endif
