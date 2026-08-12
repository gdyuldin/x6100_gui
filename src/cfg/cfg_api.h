#pragma once

// C-compatible API for the SettingsManager's parameters (Stage B). This header
// is the umbrella entry point for C (and C++) consumers: it aggregates the
// per-type C-API headers and exposes the opaque extern parameter pointers,
// filled by cfg_api_init().
//
// Per-type APIs (all included here for convenience):
//   - subject_api.h            generic Subject/SubjectT/Observer helpers
//   - parameter_api.h          Parameter<T> typed get/set
//   - computed_api.h           ComputedParameter<T> set/get
//   - atu_cache_api.h          ATU tuner-network cache
//   - settings_manager_api.h   init, flush, band/VFO switching, freq helpers
//
// All accessors route through Parameter<T>::set / SubjectT<T>::get, so
// validators, deferred-write enqueue and observer notifications all apply.
// Ownership: the parameters (and the SettingsManager singleton) are static and
// owned by C++ (cfg_api.cpp). C code only receives/holds opaque pointers and
// must never free them. Observers returned by *_subscribe are owned by C/UI
// code and freed with param_unsubscribe.
//
// cfg_api_init() does NOT open the DB or call cfg_db_init(): the caller owns
// the sqlite3 connection and table initialisation (avoids double-Init).

#include "computed_api.h"
#include "subject_api.h"
#include "parameter_api.h"
#include "atu_cache_api.h"
#include "settings_manager_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque extern globals, filled by cfg_api_init() ---
// Each maps to a concrete public SettingsManager member. Int params map to
// ParamInt, scaled-float params to ParamFloat, and p_encoder_bind (a string)
// to ParamText. The extern list is the exhaustive set of C-reachable
// parameters — every SettingsManager parameter is exposed so migrating C/C++
// consumers can read/write the same values through one API.
//
// Grouped by storage scope: GLOBAL -> BAND -> MODE -> OTHER (Transverter) ->
// COMPUTED (current operating state, not persisted).

// --- GLOBAL params (flat `params` table) ---
extern ParamInt         *cfg_volume;            // p_volume
extern ParamInt         *cfg_squelch;           // p_squelch
extern ParamInt         *cfg_rfgain;            // p_rfgain
extern ParamInt         *cfg_rit;               // p_rit
extern ParamInt         *cfg_xit;               // p_xit
extern ParamFloat       *cfg_pwr;               // p_pwr
extern ParamInt         *cfg_band_id;           // p_band_id (persisted current band)
extern ParamInt         *cfg_mic;               // p_mic
extern ParamInt         *cfg_hmic;              // p_hmic
extern ParamInt         *cfg_imic;              // p_imic
extern ParamInt         *cfg_moni;              // p_moni
extern ParamInt         *cfg_ant_id;            // p_ant_id
extern ParamInt         *cfg_atu_enabled;       // p_atu_enabled

// UI
extern ParamInt         *cfg_auto_level_enabled; // p_auto_level_enabled
extern ParamFloat       *cfg_auto_level_offset;  // p_auto_level_offset
extern ParamInt         *cfg_knob_info;          // p_knob_info
extern ParamText        *cfg_encoder_bind;       // p_encoder_bind

// VOX
extern ParamInt *cfg_vox_on;   // p_vox_en
extern ParamInt *cfg_vox_gain; // p_vox_gain
extern ParamInt *cfg_vox_ag;   // p_vox_ag
extern ParamInt *cfg_vox_delay; // p_vox_delay

// FT8
extern ParamInt *cfg_ft8_show_all;   // p_ft8_show_all
extern ParamInt *cfg_ft8_protocol;   // p_ft8_protocol
extern ParamInt *cfg_ft8_auto;       // p_ft8_auto
extern ParamInt *cfg_ft8_hold_freq;  // p_ft8_hold_freq
extern ParamInt *cfg_ft8_max_repeats; // p_ft8_max_repeats

// SWR scan
extern ParamInt *cfg_swrscan_linear; // p_swrscan_linear
extern ParamInt *cfg_swrscan_span;   // p_swrscan_span

// CW
extern ParamInt   *cfg_key_tone;            // p_key_tone
extern ParamInt   *cfg_key_speed;           // p_key_speed
extern ParamInt   *cfg_key_mode;            // p_key_mode
extern ParamInt   *cfg_iambic_mode;         // p_iambic_mode
extern ParamInt   *cfg_key_vol;             // p_key_vol
extern ParamInt   *cfg_key_train;           // p_key_train
extern ParamInt   *cfg_qsk_time;            // p_qsk_time
extern ParamFloat *cfg_key_ratio;           // p_key_ratio
extern ParamInt   *cfg_cw_peak_on;          // p_cw_peak_on
extern ParamInt   *cfg_cw_peak_q;           // p_cw_peak_q

// CW decoder
extern ParamInt   *cfg_cw_decoder;         // p_cw_decoder
extern ParamInt   *cfg_cw_tune;            // p_cw_tune
extern ParamFloat *cfg_cw_decoder_snr;     // p_cw_decoder_snr
extern ParamFloat *cfg_cw_decoder_snr_gist; // p_cw_decoder_snr_gist

// AGC
extern ParamInt *cfg_agc_hang;  // p_agc_hang
extern ParamInt *cfg_agc_knee;  // p_agc_knee
extern ParamInt *cfg_agc_slope; // p_agc_slope

// DSP
extern ParamInt *cfg_dnf;        // p_dnf
extern ParamInt *cfg_dnf_center; // p_dnf_center
extern ParamInt *cfg_dnf_width;  // p_dnf_width
extern ParamInt *cfg_dnf_auto;   // p_dnf_auto
extern ParamInt *cfg_nb;         // p_nb
extern ParamInt *cfg_nb_level;   // p_nb_level
extern ParamInt *cfg_nb_width;   // p_nb_width
extern ParamInt *cfg_nr;         // p_nr
extern ParamInt *cfg_nr_level;   // p_nr_level

// DSP custom
extern ParamFloat *cfg_output_gain;          // p_output_gain
extern ParamInt   *cfg_comp;                 // p_comp
extern ParamFloat *cfg_comp_threshold_offset; // p_comp_threshold_offset
extern ParamFloat *cfg_comp_makeup_offset;    // p_comp_makeup_offset
extern ParamInt   *cfg_fm_emphasis;          // p_fm_emphasis
extern ParamInt   *cfg_tx_filter_low;        // p_tx_filter_low
extern ParamInt   *cfg_tx_filter_high;       // p_tx_filter_high
extern ParamInt   *cfg_cessb_on;             // p_cessb_on
extern ParamFloat *cfg_cessb_power_up;       // p_cessb_power_up

// --- BAND params ---
extern ParamInt   *cfg_band_current_vfo;  // p_band_current_vfo
extern ParamInt   *cfg_band_if_shift;     // p_band_if_shift
extern ParamInt   *cfg_band_vfoa_freq;    // p_band_vfoa_freq
extern ParamInt   *cfg_band_vfob_freq;    // p_band_vfob_freq
extern ParamFloat *cfg_band_dac_offset;   // p_band_dac_offset
extern ParamInt   *cfg_band_grid_min;     // p_band_grid_min
extern ParamInt   *cfg_band_grid_max;     // p_band_grid_max
extern ParamInt   *cfg_band_split;        // p_band_split
extern ParamInt   *cfg_band_tx_i_offset;  // p_band_tx_i_offset
extern ParamInt   *cfg_band_tx_q_offset;  // p_band_tx_q_offset
extern ParamInt   *cfg_band_vfoa_mode;    // p_band_vfoa_mode
extern ParamInt   *cfg_band_vfob_mode;    // p_band_vfob_mode
extern ParamInt   *cfg_band_vfoa_att;     // p_band_vfoa_att
extern ParamInt   *cfg_band_vfob_att;     // p_band_vfob_att
extern ParamInt   *cfg_band_vfoa_pre;     // p_band_vfoa_pre
extern ParamInt   *cfg_band_vfob_pre;     // p_band_vfob_pre
extern ParamInt   *cfg_band_vfoa_agc;     // p_band_vfoa_agc
extern ParamInt   *cfg_band_vfob_agc;     // p_band_vfob_agc

// --- MODE params ---
extern ParamInt *cfg_mode_squelch;   // p_mode_squelch
extern ParamInt *cfg_mode_zoom;      // p_mode_zoom ("spectrum_factor")
extern ParamInt *cfg_mode_freq_step; // p_mode_freq_step

// --- Transverter params (OTHER storage, fixed HW conversion) ---
// Values are Hz (p_transverter_{0,1}_{from,to,shift}).
extern ParamInt *cfg_transverter_0_from;
extern ParamInt *cfg_transverter_0_to;
extern ParamInt *cfg_transverter_0_shift;
extern ParamInt *cfg_transverter_1_from;
extern ParamInt *cfg_transverter_1_to;
extern ParamInt *cfg_transverter_1_shift;

// --- Computed params (current operating state, not persisted) ---
extern ComputedParamInt *cfg_fg_freq;          // cp_fg_freq
extern ComputedParamInt *cfg_cur_mode;         // cp_cur_mode
extern ComputedParamInt *cfg_cur_agc;          // cp_cur_agc
extern ComputedParamInt *cfg_cur_att;          // cp_cur_att
extern ComputedParamInt *cfg_cur_pre;          // cp_cur_pre
extern ComputedParamInt *cfg_bg_freq;          // cp_bg_freq
extern ComputedParamInt *cfg_cur_filter_low;   // cp_cur_filter_low
extern ComputedParamInt *cfg_cur_filter_high;  // cp_cur_filter_high
extern ComputedParamInt *cfg_cur_filter_bw;    // cp_cur_filter_bw
extern ComputedParamInt *cfg_lo_offset;        // cp_lo_offset

#ifdef __cplusplus
} // extern "C"
#endif