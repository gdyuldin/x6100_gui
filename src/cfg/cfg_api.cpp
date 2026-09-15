#include "cfg_api.h"

#include "atu_api.h"
#include "settings_internal.h"

// Owned by C++ for the whole program; never deleted.
SettingsManager cfg_sm;

// Active instance behind the accessors (test seam via cfg_set_instance).
static SettingsManager *g_cfg = &cfg_sm;

SettingsManager &cfg_instance() { return *g_cfg; }

void cfg_set_instance(SettingsManager *sm) { g_cfg = sm ? sm : &cfg_sm; }

// --- Parameter handle accessors ---
// One function per C-reachable parameter. Each returns the address of the
// corresponding SettingsManager member; the parameters are static and live for
// the whole program, so the pointer is always valid (no init-order window).
// Grouped by storage scope, matching cfg_api.h.

// --- GLOBAL params (flat `params` table) ---
static ParamInt *cfg_volume(void) { return &cfg_instance().p_volume; }
static ParamInt *cfg_squelch(void) { return &cfg_instance().p_squelch; }
static ParamInt *cfg_rfgain(void) { return &cfg_instance().p_rfgain; }
static ParamInt *cfg_rit(void) { return &cfg_instance().p_rit; }
static ParamInt *cfg_xit(void) { return &cfg_instance().p_xit; }
static ParamFloat *cfg_pwr(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_pwr); }
static ParamInt *cfg_band_id(void) { return &cfg_instance().p_band_id; }
static ParamInt *cfg_mic(void) { return &cfg_instance().p_mic; }
static ParamInt *cfg_hmic(void) { return &cfg_instance().p_hmic; }
static ParamInt *cfg_imic(void) { return &cfg_instance().p_imic; }
static ParamInt *cfg_moni(void) { return &cfg_instance().p_moni; }
static ParamInt *cfg_ant_id(void) { return &cfg_instance().p_ant_id; }
static ParamInt *cfg_atu_enabled(void) { return &cfg_instance().p_atu_enabled; }
static ParamInt *cfg_cat_baud(void) { return &cfg_instance().p_cat_baud; }
static ParamInt *cfg_display_invert(void) { return &cfg_instance().p_display_invert; }
static ParamInt *cfg_auto_level_enabled(void) { return &cfg_instance().p_auto_level_enabled; }
static ParamFloat *cfg_auto_level_offset(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_auto_level_offset); }
static ParamInt *cfg_knob_info(void) { return &cfg_instance().p_knob_info; }
static ParamInt *cfg_spectrum_use_custom_color(void) { return &cfg_instance().p_spectrum_use_custom_color; }
static ParamInt *cfg_spectrum_color(void) { return &cfg_instance().p_spectrum_color; }
static ParamText *cfg_encoder_bind(void) { return &cfg_instance().p_encoder_bind; }
static ParamInt *cfg_vox_on(void) { return &cfg_instance().p_vox_en; }
static ParamInt *cfg_vox_gain(void) { return &cfg_instance().p_vox_gain; }
static ParamInt *cfg_vox_ag(void) { return &cfg_instance().p_vox_ag; }
static ParamInt *cfg_vox_delay(void) { return &cfg_instance().p_vox_delay; }
static ParamInt *cfg_ft8_show_all(void) { return &cfg_instance().p_ft8_show_all; }
static ParamInt *cfg_ft8_protocol(void) { return &cfg_instance().p_ft8_protocol; }
static ParamInt *cfg_ft8_auto(void) { return &cfg_instance().p_ft8_auto; }
static ParamInt *cfg_ft8_hold_freq(void) { return &cfg_instance().p_ft8_hold_freq; }
static ParamInt *cfg_ft8_max_repeats(void) { return &cfg_instance().p_ft8_max_repeats; }
static ParamInt *cfg_swrscan_linear(void) { return &cfg_instance().p_swrscan_linear; }
static ParamInt *cfg_swrscan_span(void) { return &cfg_instance().p_swrscan_span; }
static ParamInt *cfg_key_tone(void) { return &cfg_instance().p_key_tone; }
static ParamInt *cfg_key_speed(void) { return &cfg_instance().p_key_speed; }
static ParamInt *cfg_key_mode(void) { return &cfg_instance().p_key_mode; }
static ParamInt *cfg_iambic_mode(void) { return &cfg_instance().p_iambic_mode; }
static ParamInt *cfg_key_vol(void) { return &cfg_instance().p_key_vol; }
static ParamInt *cfg_key_train(void) { return &cfg_instance().p_key_train; }
static ParamInt *cfg_qsk_time(void) { return &cfg_instance().p_qsk_time; }
static ParamFloat *cfg_key_ratio(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_key_ratio); }
static ParamInt *cfg_cw_peak_on(void) { return &cfg_instance().p_cw_peak_on; }
static ParamInt *cfg_cw_peak_q(void) { return &cfg_instance().p_cw_peak_q; }
static ParamInt *cfg_cw_decoder(void) { return &cfg_instance().p_cw_decoder; }
static ParamInt *cfg_cw_tune(void) { return &cfg_instance().p_cw_tune; }
static ParamFloat *cfg_cw_decoder_snr(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_cw_decoder_snr); }
static ParamFloat *cfg_cw_decoder_snr_gist(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_cw_decoder_snr_gist); }
static ParamInt *cfg_agc_hang(void) { return &cfg_instance().p_agc_hang; }
static ParamInt *cfg_agc_knee(void) { return &cfg_instance().p_agc_knee; }
static ParamInt *cfg_agc_slope(void) { return &cfg_instance().p_agc_slope; }
static ParamInt *cfg_dnf(void) { return &cfg_instance().p_dnf; }
static ParamInt *cfg_dnf_center(void) { return &cfg_instance().p_dnf_center; }
static ParamInt *cfg_dnf_width(void) { return &cfg_instance().p_dnf_width; }
static ParamInt *cfg_dnf_auto(void) { return &cfg_instance().p_dnf_auto; }
static ParamInt *cfg_nb(void) { return &cfg_instance().p_nb; }
static ParamInt *cfg_nb_level(void) { return &cfg_instance().p_nb_level; }
static ParamInt *cfg_nb_width(void) { return &cfg_instance().p_nb_width; }
static ParamInt *cfg_nr(void) { return &cfg_instance().p_nr; }
static ParamInt *cfg_nr_level(void) { return &cfg_instance().p_nr_level; }
static ParamFloat *cfg_output_gain(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_output_gain); }
static ParamInt *cfg_comp(void) { return &cfg_instance().p_comp; }
static ParamFloat *cfg_comp_threshold_offset(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_comp_threshold_offset); }
static ParamFloat *cfg_comp_makeup_offset(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_comp_makeup_offset); }
static ParamInt *cfg_fm_emphasis(void) { return &cfg_instance().p_fm_emphasis; }
static ParamInt *cfg_tx_filter_low(void) { return &cfg_instance().p_tx_filter_low; }
static ParamInt *cfg_tx_filter_high(void) { return &cfg_instance().p_tx_filter_high; }
static ParamInt *cfg_cessb_on(void) { return &cfg_instance().p_cessb_on; }
static ParamFloat *cfg_cessb_power_up(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_cessb_power_up); }

// --- BAND params ---
static ParamInt *cfg_band_current_vfo(void) { return &cfg_instance().p_band_current_vfo; }
static ParamInt *cfg_band_if_shift(void) { return &cfg_instance().p_band_if_shift; }
static ParamInt *cfg_band_vfoa_freq(void) { return &cfg_instance().p_band_vfoa_freq; }
static ParamInt *cfg_band_vfob_freq(void) { return &cfg_instance().p_band_vfob_freq; }
static ParamFloat *cfg_band_dac_offset(void) { return reinterpret_cast<ParamFloat *>(&cfg_instance().p_band_dac_offset); }
static ParamInt *cfg_band_grid_min(void) { return &cfg_instance().p_band_grid_min; }
static ParamInt *cfg_band_grid_max(void) { return &cfg_instance().p_band_grid_max; }
static ParamInt *cfg_band_split(void) { return &cfg_instance().p_band_split; }
static ParamInt *cfg_band_tx_i_offset(void) { return &cfg_instance().p_band_tx_i_offset; }
static ParamInt *cfg_band_tx_q_offset(void) { return &cfg_instance().p_band_tx_q_offset; }
static ParamInt *cfg_band_vfoa_mode(void) { return &cfg_instance().p_band_vfoa_mode; }
static ParamInt *cfg_band_vfob_mode(void) { return &cfg_instance().p_band_vfob_mode; }
static ParamInt *cfg_band_vfoa_att(void) { return &cfg_instance().p_band_vfoa_att; }
static ParamInt *cfg_band_vfob_att(void) { return &cfg_instance().p_band_vfob_att; }
static ParamInt *cfg_band_vfoa_pre(void) { return &cfg_instance().p_band_vfoa_pre; }
static ParamInt *cfg_band_vfob_pre(void) { return &cfg_instance().p_band_vfob_pre; }
static ParamInt *cfg_band_vfoa_agc(void) { return &cfg_instance().p_band_vfoa_agc; }
static ParamInt *cfg_band_vfob_agc(void) { return &cfg_instance().p_band_vfob_agc; }

// --- MODE params ---
static ParamInt *cfg_mode_zoom(void) { return &cfg_instance().p_mode_zoom; }
static ParamInt *cfg_mode_freq_step(void) { return &cfg_instance().p_mode_freq_step; }

// --- Transverter params (OTHER storage, fixed HW conversion) ---
static ParamInt *cfg_transverter_0_from(void) { return &cfg_instance().p_transverter_0_from; }
static ParamInt *cfg_transverter_0_to(void) { return &cfg_instance().p_transverter_0_to; }
static ParamInt *cfg_transverter_0_shift(void) { return &cfg_instance().p_transverter_0_shift; }
static ParamInt *cfg_transverter_1_from(void) { return &cfg_instance().p_transverter_1_from; }
static ParamInt *cfg_transverter_1_to(void) { return &cfg_instance().p_transverter_1_to; }
static ParamInt *cfg_transverter_1_shift(void) { return &cfg_instance().p_transverter_1_shift; }

// --- Computed params (current operating state, not persisted) ---
static ComputedParamInt *cfg_fg_freq(void) { return &cfg_instance().cp_fg_freq; }
static ComputedParamInt *cfg_cur_mode(void) { return &cfg_instance().cp_cur_mode; }
static ComputedParamInt *cfg_cur_agc(void) { return &cfg_instance().cp_cur_agc; }
static ComputedParamInt *cfg_cur_att(void) { return &cfg_instance().cp_cur_att; }
static ComputedParamInt *cfg_cur_pre(void) { return &cfg_instance().cp_cur_pre; }
static ComputedParamInt *cfg_bg_freq(void) { return &cfg_instance().cp_bg_freq; }
static ComputedParamInt *cfg_cur_filter_low(void) { return &cfg_instance().cp_cur_filter_low; }
static ComputedParamInt *cfg_cur_filter_high(void) { return &cfg_instance().cp_cur_filter_high; }
static ComputedParamInt *cfg_cur_filter_bw(void) { return &cfg_instance().cp_cur_filter_bw; }
static ComputedParamInt *cfg_mode_lo_offset(void) { return &cfg_instance().cp_mode_lo_offset; }

extern "C" const cfg_refs_t cfg = {
    .general = {
        .volume = &cfg_volume,
        .squelch = &cfg_squelch,
        .rfgain = &cfg_rfgain,
        .rit = &cfg_rit,
        .xit = &cfg_xit,
        .pwr = &cfg_pwr,
        .band_id = &cfg_band_id,
        .mic = &cfg_mic,
        .hmic = &cfg_hmic,
        .imic = &cfg_imic,
        .moni = &cfg_moni,
        .ant_id = &cfg_ant_id,
        .atu_enabled = &cfg_atu_enabled,
        .cat_baud = &cfg_cat_baud,
        .display_invert = &cfg_display_invert,
    },
    .spectrum = {
        .auto_level_enabled = &cfg_auto_level_enabled,
        .auto_level_offset = &cfg_auto_level_offset,
        .knob_info = &cfg_knob_info,
        .spectrum_use_custom_color = &cfg_spectrum_use_custom_color,
        .spectrum_color = &cfg_spectrum_color,
    },
    .encoder = {
        .bind = &cfg_encoder_bind,
    },
    .vox = {
        .on = &cfg_vox_on,
        .gain = &cfg_vox_gain,
        .ag = &cfg_vox_ag,
        .delay = &cfg_vox_delay,
    },
    .ft8 = {
        .show_all = &cfg_ft8_show_all,
        .protocol = &cfg_ft8_protocol,
        .auto_mode = &cfg_ft8_auto,
        .hold_freq = &cfg_ft8_hold_freq,
        .max_repeats = &cfg_ft8_max_repeats,
    },
    .swrscan = {
        .linear = &cfg_swrscan_linear,
        .span = &cfg_swrscan_span,
    },
    .cw = {
        .key_tone = &cfg_key_tone,
        .key_speed = &cfg_key_speed,
        .key_mode = &cfg_key_mode,
        .iambic_mode = &cfg_iambic_mode,
        .key_vol = &cfg_key_vol,
        .key_train = &cfg_key_train,
        .qsk_time = &cfg_qsk_time,
        .key_ratio = &cfg_key_ratio,
        .peak_on = &cfg_cw_peak_on,
        .peak_q = &cfg_cw_peak_q,
        .decoder = &cfg_cw_decoder,
        .tune = &cfg_cw_tune,
        .decoder_snr = &cfg_cw_decoder_snr,
        .decoder_snr_gist = &cfg_cw_decoder_snr_gist,
    },
    .agc = {
        .hang = &cfg_agc_hang,
        .knee = &cfg_agc_knee,
        .slope = &cfg_agc_slope,
    },
    .dsp = {
        .dnf = &cfg_dnf,
        .dnf_center = &cfg_dnf_center,
        .dnf_width = &cfg_dnf_width,
        .dnf_auto = &cfg_dnf_auto,
        .nb = &cfg_nb,
        .nb_level = &cfg_nb_level,
        .nb_width = &cfg_nb_width,
        .nr = &cfg_nr,
        .nr_level = &cfg_nr_level,
        .output_gain = &cfg_output_gain,
        .comp = &cfg_comp,
        .comp_threshold_offset = &cfg_comp_threshold_offset,
        .comp_makeup_offset = &cfg_comp_makeup_offset,
        .fm_emphasis = &cfg_fm_emphasis,
        .tx_filter_low = &cfg_tx_filter_low,
        .tx_filter_high = &cfg_tx_filter_high,
        .cessb_on = &cfg_cessb_on,
        .cessb_power_up = &cfg_cessb_power_up,
    },
    .band = {
        .current_vfo = &cfg_band_current_vfo,
        .if_shift = &cfg_band_if_shift,
        .vfoa_freq = &cfg_band_vfoa_freq,
        .vfob_freq = &cfg_band_vfob_freq,
        .dac_offset = &cfg_band_dac_offset,
        .grid_min = &cfg_band_grid_min,
        .grid_max = &cfg_band_grid_max,
        .split = &cfg_band_split,
        .tx_i_offset = &cfg_band_tx_i_offset,
        .tx_q_offset = &cfg_band_tx_q_offset,
        .vfoa_mode = &cfg_band_vfoa_mode,
        .vfob_mode = &cfg_band_vfob_mode,
        .vfoa_att = &cfg_band_vfoa_att,
        .vfob_att = &cfg_band_vfob_att,
        .vfoa_pre = &cfg_band_vfoa_pre,
        .vfob_pre = &cfg_band_vfob_pre,
        .vfoa_agc = &cfg_band_vfoa_agc,
        .vfob_agc = &cfg_band_vfob_agc,
    },
    .mode = {
        .zoom = &cfg_mode_zoom,
        .freq_step = &cfg_mode_freq_step,
    },
    .filter = {
        .low = &cfg_cur_filter_low,
        .high = &cfg_cur_filter_high,
        .bw = &cfg_cur_filter_bw,
    },
    .computed = {
        .fg_freq = &cfg_fg_freq,
        .mode = &cfg_cur_mode,
        .agc = &cfg_cur_agc,
        .att = &cfg_cur_att,
        .pre = &cfg_cur_pre,
        .bg_freq = &cfg_bg_freq,
        .mode_lo_offset = &cfg_mode_lo_offset,
    },
    .transverter = {
        .t0_from = &cfg_transverter_0_from,
        .t0_to = &cfg_transverter_0_to,
        .t0_shift = &cfg_transverter_0_shift,
        .t1_from = &cfg_transverter_1_from,
        .t1_to = &cfg_transverter_1_to,
        .t1_shift = &cfg_transverter_1_shift,
    },
};

void cfg_api_init(void (*on_db_error)(const char *)) {
    // The manager loads global/band/mode params; the accessors above need no
    // wiring.
    cfg_sm.init_load(on_db_error);

    // Wire the ATU cache to the parameter sources and do the initial load.
    atu_wire_subscriptions();
}
