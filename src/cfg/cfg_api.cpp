#include "cfg_api.h"

#include "atu_cache_api.h"
#include "settings_manager.h"

// The opaque C handles are the concrete Parameter<T> / ComputedParameter<T>
// instantiations owned by the static SettingsManager below. Casts are safe
// because C code only ever receives them through this API.

// Owned by C++ for the whole program; never deleted.
SettingsManager cfg_sm;

// --- extern globals (declared in cfg_api.h), filled by cfg_api_init() ---
// Grouped by storage scope, matching cfg_api.h: GLOBAL -> BAND -> MODE -> OTHER
// (Transverter) -> COMPUTED.

// GLOBAL
ParamInt         *cfg_volume              = nullptr;
ParamInt         *cfg_squelch             = nullptr;
ParamInt         *cfg_rfgain              = nullptr;
ParamInt         *cfg_rit                 = nullptr;
ParamInt         *cfg_xit                 = nullptr;
ParamFloat       *cfg_pwr                 = nullptr;
ParamInt         *cfg_band_id             = nullptr;
ParamInt         *cfg_mic                 = nullptr;
ParamInt         *cfg_hmic                = nullptr;
ParamInt         *cfg_imic                = nullptr;
ParamInt         *cfg_moni                = nullptr;
ParamInt         *cfg_ant_id              = nullptr;
ParamInt         *cfg_atu_enabled         = nullptr;
ParamInt         *cfg_auto_level_enabled  = nullptr;
ParamFloat       *cfg_auto_level_offset   = nullptr;
ParamInt         *cfg_knob_info           = nullptr;
ParamText        *cfg_encoder_bind        = nullptr;
ParamInt         *cfg_vox_on              = nullptr;
ParamInt         *cfg_vox_gain            = nullptr;
ParamInt         *cfg_vox_ag              = nullptr;
ParamInt         *cfg_vox_delay           = nullptr;
ParamInt         *cfg_ft8_show_all        = nullptr;
ParamInt         *cfg_ft8_protocol        = nullptr;
ParamInt         *cfg_ft8_auto            = nullptr;
ParamInt         *cfg_ft8_hold_freq       = nullptr;
ParamInt         *cfg_ft8_max_repeats     = nullptr;
ParamInt         *cfg_swrscan_linear      = nullptr;
ParamInt         *cfg_swrscan_span        = nullptr;
ParamInt         *cfg_key_tone            = nullptr;
ParamInt         *cfg_key_speed           = nullptr;
ParamInt         *cfg_key_mode            = nullptr;
ParamInt         *cfg_iambic_mode         = nullptr;
ParamInt         *cfg_key_vol             = nullptr;
ParamInt         *cfg_key_train           = nullptr;
ParamInt         *cfg_qsk_time            = nullptr;
ParamFloat       *cfg_key_ratio           = nullptr;
ParamInt         *cfg_cw_peak_on          = nullptr;
ParamInt         *cfg_cw_peak_q           = nullptr;
ParamInt         *cfg_cw_decoder          = nullptr;
ParamInt         *cfg_cw_tune             = nullptr;
ParamFloat       *cfg_cw_decoder_snr      = nullptr;
ParamFloat       *cfg_cw_decoder_snr_gist = nullptr;
ParamInt         *cfg_agc_hang            = nullptr;
ParamInt         *cfg_agc_knee            = nullptr;
ParamInt         *cfg_agc_slope           = nullptr;
ParamInt         *cfg_dnf                 = nullptr;
ParamInt         *cfg_dnf_center          = nullptr;
ParamInt         *cfg_dnf_width           = nullptr;
ParamInt         *cfg_dnf_auto            = nullptr;
ParamInt         *cfg_nb                  = nullptr;
ParamInt         *cfg_nb_level            = nullptr;
ParamInt         *cfg_nb_width            = nullptr;
ParamInt         *cfg_nr                  = nullptr;
ParamInt         *cfg_nr_level            = nullptr;
ParamFloat       *cfg_output_gain         = nullptr;
ParamInt         *cfg_comp                = nullptr;
ParamFloat       *cfg_comp_threshold_offset = nullptr;
ParamFloat       *cfg_comp_makeup_offset    = nullptr;
ParamInt         *cfg_fm_emphasis         = nullptr;
ParamInt         *cfg_tx_filter_low       = nullptr;
ParamInt         *cfg_tx_filter_high      = nullptr;
ParamInt         *cfg_cessb_on            = nullptr;
ParamFloat       *cfg_cessb_power_up      = nullptr;

// BAND
ParamInt         *cfg_band_current_vfo    = nullptr;
ParamInt         *cfg_band_if_shift       = nullptr;
ParamInt         *cfg_band_vfoa_freq      = nullptr;
ParamInt         *cfg_band_vfob_freq      = nullptr;
ParamFloat       *cfg_band_dac_offset     = nullptr;
ParamInt         *cfg_band_grid_min       = nullptr;
ParamInt         *cfg_band_grid_max       = nullptr;
ParamInt         *cfg_band_split          = nullptr;
ParamInt         *cfg_band_tx_i_offset    = nullptr;
ParamInt         *cfg_band_tx_q_offset    = nullptr;
ParamInt         *cfg_band_vfoa_mode      = nullptr;
ParamInt         *cfg_band_vfob_mode      = nullptr;
ParamInt         *cfg_band_vfoa_att       = nullptr;
ParamInt         *cfg_band_vfob_att       = nullptr;
ParamInt         *cfg_band_vfoa_pre       = nullptr;
ParamInt         *cfg_band_vfob_pre       = nullptr;
ParamInt         *cfg_band_vfoa_agc       = nullptr;
ParamInt         *cfg_band_vfob_agc       = nullptr;

// MODE
ParamInt         *cfg_mode_squelch        = nullptr;
ParamInt         *cfg_mode_zoom           = nullptr;
ParamInt         *cfg_mode_freq_step      = nullptr;

// TRANSVERTER (OTHER)
ParamInt         *cfg_transverter_0_from  = nullptr;
ParamInt         *cfg_transverter_0_to    = nullptr;
ParamInt         *cfg_transverter_0_shift = nullptr;
ParamInt         *cfg_transverter_1_from  = nullptr;
ParamInt         *cfg_transverter_1_to    = nullptr;
ParamInt         *cfg_transverter_1_shift = nullptr;

// COMPUTED (current operating state)
ComputedParamInt *cfg_fg_freq             = nullptr;
ComputedParamInt *cfg_cur_mode            = nullptr;
ComputedParamInt *cfg_cur_agc             = nullptr;
ComputedParamInt *cfg_cur_att             = nullptr;
ComputedParamInt *cfg_cur_pre             = nullptr;
ComputedParamInt *cfg_bg_freq             = nullptr;
ComputedParamInt *cfg_cur_filter_low      = nullptr;
ComputedParamInt *cfg_cur_filter_high     = nullptr;
ComputedParamInt *cfg_cur_filter_bw       = nullptr;
ComputedParamInt *cfg_lo_offset           = nullptr;

void cfg_api_init(void (*on_db_error)(const char *)) {
    // on_db_error is kept for signature compatibility but is currently inert
    // (plan A.1 dropped the wiring): the manager stores it but never invokes it.
    // The starting band/mode are derived internally (persisted global band_id +
    // cp_cur_mode).
    cfg_sm.init_load(on_db_error);

    // GLOBAL
    cfg_volume           = &cfg_sm.p_volume;
    cfg_squelch          = &cfg_sm.p_squelch;
    cfg_rfgain           = &cfg_sm.p_rfgain;
    cfg_rit              = &cfg_sm.p_rit;
    cfg_xit              = &cfg_sm.p_xit;
    cfg_pwr              = reinterpret_cast<ParamFloat *>(&cfg_sm.p_pwr);
    cfg_band_id          = &cfg_sm.p_band_id;
    cfg_mic              = &cfg_sm.p_mic;
    cfg_hmic             = &cfg_sm.p_hmic;
    cfg_imic             = &cfg_sm.p_imic;
    cfg_moni             = &cfg_sm.p_moni;
    cfg_ant_id           = &cfg_sm.p_ant_id;
    cfg_atu_enabled      = &cfg_sm.p_atu_enabled;
    cfg_auto_level_enabled = &cfg_sm.p_auto_level_enabled;
    cfg_auto_level_offset  = reinterpret_cast<ParamFloat *>(&cfg_sm.p_auto_level_offset);
    cfg_knob_info        = &cfg_sm.p_knob_info;
    cfg_encoder_bind     = &cfg_sm.p_encoder_bind;
    cfg_vox_on           = &cfg_sm.p_vox_en;
    cfg_vox_gain         = &cfg_sm.p_vox_gain;
    cfg_vox_ag           = &cfg_sm.p_vox_ag;
    cfg_vox_delay        = &cfg_sm.p_vox_delay;
    cfg_ft8_show_all     = &cfg_sm.p_ft8_show_all;
    cfg_ft8_protocol     = &cfg_sm.p_ft8_protocol;
    cfg_ft8_auto         = &cfg_sm.p_ft8_auto;
    cfg_ft8_hold_freq    = &cfg_sm.p_ft8_hold_freq;
    cfg_ft8_max_repeats  = &cfg_sm.p_ft8_max_repeats;
    cfg_swrscan_linear   = &cfg_sm.p_swrscan_linear;
    cfg_swrscan_span     = &cfg_sm.p_swrscan_span;
    cfg_key_tone         = &cfg_sm.p_key_tone;
    cfg_key_speed        = &cfg_sm.p_key_speed;
    cfg_key_mode         = &cfg_sm.p_key_mode;
    cfg_iambic_mode      = &cfg_sm.p_iambic_mode;
    cfg_key_vol          = &cfg_sm.p_key_vol;
    cfg_key_train        = &cfg_sm.p_key_train;
    cfg_qsk_time         = &cfg_sm.p_qsk_time;
    cfg_key_ratio        = reinterpret_cast<ParamFloat *>(&cfg_sm.p_key_ratio);
    cfg_cw_peak_on       = &cfg_sm.p_cw_peak_on;
    cfg_cw_peak_q        = &cfg_sm.p_cw_peak_q;
    cfg_cw_decoder       = &cfg_sm.p_cw_decoder;
    cfg_cw_tune          = &cfg_sm.p_cw_tune;
    cfg_cw_decoder_snr   = reinterpret_cast<ParamFloat *>(&cfg_sm.p_cw_decoder_snr);
    cfg_cw_decoder_snr_gist = reinterpret_cast<ParamFloat *>(&cfg_sm.p_cw_decoder_snr_gist);
    cfg_agc_hang         = &cfg_sm.p_agc_hang;
    cfg_agc_knee         = &cfg_sm.p_agc_knee;
    cfg_agc_slope        = &cfg_sm.p_agc_slope;
    cfg_dnf              = &cfg_sm.p_dnf;
    cfg_dnf_center       = &cfg_sm.p_dnf_center;
    cfg_dnf_width        = &cfg_sm.p_dnf_width;
    cfg_dnf_auto         = &cfg_sm.p_dnf_auto;
    cfg_nb               = &cfg_sm.p_nb;
    cfg_nb_level         = &cfg_sm.p_nb_level;
    cfg_nb_width         = &cfg_sm.p_nb_width;
    cfg_nr               = &cfg_sm.p_nr;
    cfg_nr_level         = &cfg_sm.p_nr_level;
    cfg_output_gain      = reinterpret_cast<ParamFloat *>(&cfg_sm.p_output_gain);
    cfg_comp             = &cfg_sm.p_comp;
    cfg_comp_threshold_offset = reinterpret_cast<ParamFloat *>(&cfg_sm.p_comp_threshold_offset);
    cfg_comp_makeup_offset    = reinterpret_cast<ParamFloat *>(&cfg_sm.p_comp_makeup_offset);
    cfg_fm_emphasis      = &cfg_sm.p_fm_emphasis;
    cfg_tx_filter_low    = &cfg_sm.p_tx_filter_low;
    cfg_tx_filter_high   = &cfg_sm.p_tx_filter_high;
    cfg_cessb_on         = &cfg_sm.p_cessb_on;
    cfg_cessb_power_up   = reinterpret_cast<ParamFloat *>(&cfg_sm.p_cessb_power_up);

    // BAND
    cfg_band_current_vfo = &cfg_sm.p_band_current_vfo;
    cfg_band_if_shift    = &cfg_sm.p_band_if_shift;
    cfg_band_vfoa_freq   = &cfg_sm.p_band_vfoa_freq;
    cfg_band_vfob_freq   = &cfg_sm.p_band_vfob_freq;
    cfg_band_dac_offset  = reinterpret_cast<ParamFloat *>(&cfg_sm.p_band_dac_offset);
    cfg_band_grid_min    = &cfg_sm.p_band_grid_min;
    cfg_band_grid_max    = &cfg_sm.p_band_grid_max;
    cfg_band_split       = &cfg_sm.p_band_split;
    cfg_band_tx_i_offset = &cfg_sm.p_band_tx_i_offset;
    cfg_band_tx_q_offset = &cfg_sm.p_band_tx_q_offset;
    cfg_band_vfoa_mode   = &cfg_sm.p_band_vfoa_mode;
    cfg_band_vfob_mode   = &cfg_sm.p_band_vfob_mode;
    cfg_band_vfoa_att    = &cfg_sm.p_band_vfoa_att;
    cfg_band_vfob_att    = &cfg_sm.p_band_vfob_att;
    cfg_band_vfoa_pre    = &cfg_sm.p_band_vfoa_pre;
    cfg_band_vfob_pre    = &cfg_sm.p_band_vfob_pre;
    cfg_band_vfoa_agc    = &cfg_sm.p_band_vfoa_agc;
    cfg_band_vfob_agc    = &cfg_sm.p_band_vfob_agc;

    // MODE
    cfg_mode_squelch     = &cfg_sm.p_mode_squelch;
    cfg_mode_zoom        = &cfg_sm.p_mode_zoom;
    cfg_mode_freq_step   = &cfg_sm.p_mode_freq_step;

    // TRANSVERTER (OTHER)
    cfg_transverter_0_from  = &cfg_sm.p_transverter_0_from;
    cfg_transverter_0_to    = &cfg_sm.p_transverter_0_to;
    cfg_transverter_0_shift = &cfg_sm.p_transverter_0_shift;
    cfg_transverter_1_from  = &cfg_sm.p_transverter_1_from;
    cfg_transverter_1_to    = &cfg_sm.p_transverter_1_to;
    cfg_transverter_1_shift = &cfg_sm.p_transverter_1_shift;

    // COMPUTED (current operating state)
    cfg_fg_freq          = &cfg_sm.cp_fg_freq;
    cfg_cur_mode         = &cfg_sm.cp_cur_mode;
    cfg_cur_agc          = &cfg_sm.cp_cur_agc;
    cfg_cur_att          = &cfg_sm.cp_cur_att;
    cfg_cur_pre          = &cfg_sm.cp_cur_pre;
    cfg_bg_freq          = &cfg_sm.cp_bg_freq;
    cfg_cur_filter_low   = &cfg_sm.cp_cur_filter_low;
    cfg_cur_filter_high  = &cfg_sm.cp_cur_filter_high;
    cfg_cur_filter_bw    = &cfg_sm.cp_cur_filter_bw;
    cfg_lo_offset        = &cfg_sm.cp_lo_offset;

    // Wire the ATU cache to the parameter sources and do the initial load.
    atu_cache_wire_subscriptions();
}