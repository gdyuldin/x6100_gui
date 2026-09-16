#pragma once

// C-compatible API for the SettingsManager's parameters. This header is the
// umbrella entry point for C (and C++) consumers: it aggregates the per-type
// C-API headers and exposes the parameter access tree `cfg`.
//
// Per-type APIs (all included here for convenience):
//   - subject_api.h            generic Subject/SubjectT/Observer helpers
//   - parameter_api.h          Parameter<T> typed get/set
//   - computed_api.h           ComputedParameter<T> set/get
//   - atu_api.h                ATU tuner-network cache
//   - settings_manager_api.h   init, flush, band/VFO switching, freq helpers
//
// Access is through cfg.<group>.<name>(), which returns the Parameter<T>*
// handle (opaque in C, concrete in C++). All writes route through
// Parameter<T>::set, so validators, deferred-write enqueue and observer
// notifications all apply.
// Ownership: the parameters (and the SettingsManager singleton) are static and
// owned by C++ (cfg_api.cpp). C code only receives/holds borrowed pointers and
// must never free them. Observers returned by subject_*_subscribe are borrowed:
// the Subject owns one reference while the observer stays subscribed; C/UI code
// releases that reference with param_unsubscribe.
//
// cfg_api_init() does NOT open the DB or call cfg_db_init(): the caller owns
// the sqlite3 connection and table initialisation (avoids double-Init).

#include "computed_api.h"
#include "subject_api.h"
#include "parameter_api.h"
#include "atu_api.h"
#include "settings_manager_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Parameter access tree ---
// cfg.<group>.<name>() returns the handle of the matching SettingsManager
// parameter, grouped for discoverability. Works identically from C and C++; in
// C++ the returned Parameter<T>* additionally supports ->get()/->set()/
// ->subscribe(). The member name in SettingsManager is shown in the comment.

typedef struct {
    ParamInt *(*volume)(void); /* p_volume */
    ParamInt *(*squelch)(void); /* p_squelch */
    ParamInt *(*rfgain)(void); /* p_rfgain */
    ParamInt *(*rit)(void); /* p_rit */
    ParamInt *(*xit)(void); /* p_xit */
    ParamFloat *(*pwr)(void); /* p_pwr */
    ParamInt *(*band_id)(void); /* p_band_id */
    ParamInt *(*mic)(void); /* p_mic */
    ParamInt *(*hmic)(void); /* p_hmic */
    ParamInt *(*imic)(void); /* p_imic */
    ParamInt *(*moni)(void); /* p_moni */
    ParamInt *(*ant_id)(void); /* p_ant_id */
    ParamInt *(*atu_enabled)(void); /* p_atu_enabled */
    ParamInt *(*cat_baud)(void); /* p_cat_baud */
    ParamInt *(*display_invert)(void); /* p_display_invert */
} cfg_general_refs_t;

typedef struct {
    ParamInt *(*auto_level_enabled)(void); /* p_auto_level_enabled */
    ParamFloat *(*auto_level_offset)(void); /* p_auto_level_offset */
    ParamInt *(*knob_info)(void); /* p_knob_info */
    ParamInt *(*spectrum_use_custom_color)(void); /* p_spectrum_use_custom_color */
    ParamInt *(*spectrum_color)(void); /* p_spectrum_color */
    ParamInt *(*beta)(void); /* p_spectrum_beta */
    ParamInt *(*peak)(void); /* p_spectrum_peak */
    ParamInt *(*peak_hold)(void); /* p_spectrum_peak_hold */
    ParamInt *(*peak_speed)(void); /* p_spectrum_peak_speed */
    ParamInt *(*filled)(void); /* p_spectrum_filled */
} cfg_spectrum_refs_t;

typedef struct {
    ParamInt *(*brightness_normal)(void); /* p_brightness_normal */
    ParamInt *(*brightness_idle)(void); /* p_brightness_idle */
    ParamInt *(*brightness_timeout)(void); /* p_brightness_timeout */
    ParamInt *(*brightness_buttons)(void); /* p_brightness_buttons */
} cfg_display_refs_t;

typedef struct {
    ParamInt *(*view)(void); /* p_clock_view */
    ParamInt *(*time_timeout)(void); /* p_clock_time_timeout */
    ParamInt *(*power_timeout)(void); /* p_clock_power_timeout */
    ParamInt *(*tx_timeout)(void); /* p_clock_tx_timeout */
} cfg_clock_refs_t;

typedef struct {
    ParamInt *(*center_line)(void); /* p_waterfall_center_line */
    ParamInt *(*zoom)(void); /* p_waterfall_zoom */
} cfg_waterfall_refs_t;

typedef struct {
    ParamInt *(*mag_freq)(void); /* p_mag_freq */
    ParamInt *(*mag_info)(void); /* p_mag_info */
    ParamInt *(*mag_alc)(void); /* p_mag_alc */
} cfg_view_refs_t;

typedef struct {
    ParamInt *(*mode)(void); /* p_voice_mode */
    ParamInt *(*lang)(void); /* p_voice_lang */
    ParamInt *(*rate)(void); /* p_voice_rate */
    ParamInt *(*pitch)(void); /* p_voice_pitch */
    ParamInt *(*volume)(void); /* p_voice_volume */
    ParamInt *(*msg_period)(void); /* p_voice_msg_period */
} cfg_voice_refs_t;

typedef struct {
    ParamFloat *(*play_gain_db)(void); /* p_play_gain_db */
    ParamFloat *(*rec_gain_db)(void); /* p_rec_gain_db */
} cfg_audio_refs_t;

typedef struct {
    ParamInt *(*center)(void); /* p_rtty_center */
    ParamInt *(*shift)(void); /* p_rtty_shift */
    ParamInt *(*rate)(void); /* p_rtty_rate */
    ParamInt *(*reverse)(void); /* p_rtty_reverse */
} cfg_rtty_refs_t;

typedef struct {
    ParamText *(*qth)(void); /* p_qth */
    ParamText *(*callsign)(void); /* p_callsign */
} cfg_station_refs_t;

typedef struct {
    ParamInt *(*wifi_enabled)(void); /* p_wifi_enabled */
} cfg_network_refs_t;

typedef struct {
    ParamInt *(*long_gen)(void); /* p_long_gen */
    ParamInt *(*long_app)(void); /* p_long_app */
    ParamInt *(*long_key)(void); /* p_long_key */
    ParamInt *(*long_msg)(void); /* p_long_msg */
    ParamInt *(*long_dfn)(void); /* p_long_dfn */
    ParamInt *(*long_dfl)(void); /* p_long_dfl */
    ParamInt *(*press_f1)(void); /* p_press_f1 */
    ParamInt *(*press_f2)(void); /* p_press_f2 */
    ParamInt *(*long_f1)(void); /* p_long_f1 */
    ParamInt *(*long_f2)(void); /* p_long_f2 */
} cfg_keys_refs_t;

typedef struct {
    ParamInt *(*charger)(void); /* p_charger */
    ParamInt *(*line_in)(void); /* p_line_in */
    ParamInt *(*line_out)(void); /* p_line_out */
    ParamInt *(*spmode)(void); /* p_spmode */
    ParamInt *(*freq_accel)(void); /* p_freq_accel */
} cfg_radio_refs_t;

typedef struct {
    ParamInt *(*theme)(void); /* p_theme */
    ParamInt *(*meter_color)(void); /* p_meter_color */
    ParamInt *(*swr_color)(void); /* p_swr_color */
} cfg_appearance_refs_t;

typedef struct {
    ParamText *(*bind)(void); /* p_encoder_bind */
} cfg_encoder_refs_t;

typedef struct {
    ParamInt *(*on)(void); /* p_vox_en */
    ParamInt *(*gain)(void); /* p_vox_gain */
    ParamInt *(*ag)(void); /* p_vox_ag */
    ParamInt *(*delay)(void); /* p_vox_delay */
} cfg_vox_refs_t;

typedef struct {
    ParamInt *(*show_all)(void); /* p_ft8_show_all */
    ParamInt *(*protocol)(void); /* p_ft8_protocol */
    ParamInt *(*auto_mode)(void); /* p_ft8_auto */
    ParamInt *(*hold_freq)(void); /* p_ft8_hold_freq */
    ParamInt *(*max_repeats)(void); /* p_ft8_max_repeats */
    ParamInt *(*tx_freq)(void); /* p_ft8_tx_freq */
    ParamFloat *(*output_gain_offset)(void); /* p_ft8_output_gain_offset */
    ParamText *(*cq_modifier)(void); /* p_ft8_cq_modifier */
} cfg_ft8_refs_t;

typedef struct {
    ParamInt *(*linear)(void); /* p_swrscan_linear */
    ParamInt *(*span)(void); /* p_swrscan_span */
} cfg_swrscan_refs_t;

typedef struct {
    ParamInt *(*key_tone)(void); /* p_key_tone */
    ParamInt *(*key_speed)(void); /* p_key_speed */
    ParamInt *(*key_mode)(void); /* p_key_mode */
    ParamInt *(*iambic_mode)(void); /* p_iambic_mode */
    ParamInt *(*key_vol)(void); /* p_key_vol */
    ParamInt *(*key_train)(void); /* p_key_train */
    ParamInt *(*qsk_time)(void); /* p_qsk_time */
    ParamFloat *(*key_ratio)(void); /* p_key_ratio */
    ParamInt *(*peak_on)(void); /* p_cw_peak_on */
    ParamInt *(*peak_q)(void); /* p_cw_peak_q */
    ParamInt *(*decoder)(void); /* p_cw_decoder */
    ParamInt *(*tune)(void); /* p_cw_tune */
    ParamFloat *(*decoder_snr)(void); /* p_cw_decoder_snr */
    ParamFloat *(*decoder_snr_gist)(void); /* p_cw_decoder_snr_gist */
    ParamInt *(*encoder_period)(void); /* p_cw_encoder_period */
} cfg_cw_refs_t;

typedef struct {
    ParamInt *(*hang)(void); /* p_agc_hang */
    ParamInt *(*knee)(void); /* p_agc_knee */
    ParamInt *(*slope)(void); /* p_agc_slope */
} cfg_agc_refs_t;

typedef struct {
    ParamInt *(*dnf)(void); /* p_dnf */
    ParamInt *(*dnf_center)(void); /* p_dnf_center */
    ParamInt *(*dnf_width)(void); /* p_dnf_width */
    ParamInt *(*dnf_auto)(void); /* p_dnf_auto */
    ParamInt *(*nb)(void); /* p_nb */
    ParamInt *(*nb_level)(void); /* p_nb_level */
    ParamInt *(*nb_width)(void); /* p_nb_width */
    ParamInt *(*nr)(void); /* p_nr */
    ParamInt *(*nr_level)(void); /* p_nr_level */
    ParamFloat *(*output_gain)(void); /* p_output_gain */
    ParamInt *(*comp)(void); /* p_comp */
    ParamFloat *(*comp_threshold_offset)(void); /* p_comp_threshold_offset */
    ParamFloat *(*comp_makeup_offset)(void); /* p_comp_makeup_offset */
    ParamInt *(*fm_emphasis)(void); /* p_fm_emphasis */
    ParamInt *(*tx_filter_low)(void); /* p_tx_filter_low */
    ParamInt *(*tx_filter_high)(void); /* p_tx_filter_high */
    ParamInt *(*cessb_on)(void); /* p_cessb_on */
    ParamFloat *(*cessb_power_up)(void); /* p_cessb_power_up */
} cfg_dsp_refs_t;

typedef struct {
    ParamInt *(*current_vfo)(void); /* p_band_current_vfo */
    ParamInt *(*if_shift)(void); /* p_band_if_shift */
    ParamInt *(*vfoa_freq)(void); /* p_band_vfoa_freq */
    ParamInt *(*vfob_freq)(void); /* p_band_vfob_freq */
    ParamFloat *(*dac_offset)(void); /* p_band_dac_offset */
    ParamInt *(*grid_min)(void); /* p_band_grid_min */
    ParamInt *(*grid_max)(void); /* p_band_grid_max */
    ParamInt *(*split)(void); /* p_band_split */
    ParamInt *(*tx_i_offset)(void); /* p_band_tx_i_offset */
    ParamInt *(*tx_q_offset)(void); /* p_band_tx_q_offset */
    ParamInt *(*vfoa_mode)(void); /* p_band_vfoa_mode */
    ParamInt *(*vfob_mode)(void); /* p_band_vfob_mode */
    ParamInt *(*vfoa_att)(void); /* p_band_vfoa_att */
    ParamInt *(*vfob_att)(void); /* p_band_vfob_att */
    ParamInt *(*vfoa_pre)(void); /* p_band_vfoa_pre */
    ParamInt *(*vfob_pre)(void); /* p_band_vfob_pre */
    ParamInt *(*vfoa_agc)(void); /* p_band_vfoa_agc */
    ParamInt *(*vfob_agc)(void); /* p_band_vfob_agc */
} cfg_band_refs_t;

typedef struct {
    ParamInt *(*zoom)(void); /* p_mode_zoom */
    ParamInt *(*freq_step)(void); /* p_mode_freq_step */
} cfg_mode_refs_t;

typedef struct {
    ComputedParamInt *(*low)(void); /* cp_cur_filter_low */
    ComputedParamInt *(*high)(void); /* cp_cur_filter_high */
    ComputedParamInt *(*bw)(void); /* cp_cur_filter_bw */
} cfg_filter_refs_t;

typedef struct {
    ComputedParamInt *(*fg_freq)(void); /* cp_fg_freq */
    ComputedParamInt *(*mode)(void); /* cp_cur_mode */
    ComputedParamInt *(*agc)(void); /* cp_cur_agc */
    ComputedParamInt *(*att)(void); /* cp_cur_att */
    ComputedParamInt *(*pre)(void); /* cp_cur_pre */
    ComputedParamInt *(*bg_freq)(void); /* cp_bg_freq */
    ComputedParamInt *(*mode_lo_offset)(void); /* cp_mode_lo_offset */
} cfg_computed_refs_t;

typedef struct {
    ParamInt *(*t0_from)(void); /* p_transverter_0_from */
    ParamInt *(*t0_to)(void); /* p_transverter_0_to */
    ParamInt *(*t0_shift)(void); /* p_transverter_0_shift */
    ParamInt *(*t1_from)(void); /* p_transverter_1_from */
    ParamInt *(*t1_to)(void); /* p_transverter_1_to */
    ParamInt *(*t1_shift)(void); /* p_transverter_1_shift */
} cfg_transverter_refs_t;

typedef struct {
    cfg_general_refs_t general;
    cfg_spectrum_refs_t spectrum;
    cfg_encoder_refs_t encoder;
    cfg_vox_refs_t vox;
    cfg_ft8_refs_t ft8;
    cfg_swrscan_refs_t swrscan;
    cfg_cw_refs_t cw;
    cfg_agc_refs_t agc;
    cfg_dsp_refs_t dsp;
    cfg_band_refs_t band;
    cfg_mode_refs_t mode;
    cfg_filter_refs_t filter;
    cfg_computed_refs_t computed;
    cfg_transverter_refs_t transverter;
    cfg_display_refs_t display;
    cfg_clock_refs_t clock;
    cfg_waterfall_refs_t waterfall;
    cfg_view_refs_t view;
    cfg_voice_refs_t voice;
    cfg_audio_refs_t audio;
    cfg_rtty_refs_t rtty;
    cfg_station_refs_t station;
    cfg_network_refs_t network;
    cfg_keys_refs_t keys;
    cfg_radio_refs_t radio;
    cfg_appearance_refs_t appearance;
} cfg_refs_t;

extern const cfg_refs_t cfg;

#ifdef __cplusplus
} // extern "C"
#endif
