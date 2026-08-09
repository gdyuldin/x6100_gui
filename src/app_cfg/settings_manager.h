#pragma once

// SettingsManager: orchestrator that owns the global/band/mode parameters and
// the deferred-write queue (PendingWrites), and implements band/mode switching
// semantics.
//
// Scope of this header (project rules):
//   - Only src/app_cfg/ dependencies: parameter.h, pending_writes.h,
//     computed_parameter.h, storage_policy.h, db.h.
//   - Parameters live statically (owned by the manager, never deleted) and
//     are wired to the pending-write queue at construction time.
//   - No exceptions, C++17 only.
//
// The manager is not thread-safe itself: the UI / C-API layer serialises
// access (single event loop). Individual SubjectT/Parameter values still
// protect themselves with their own mutexes, and PendingWrites has its own
// mutex, so concurrent readers of parameter values remain safe.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <ft8lib/constants.h>

#include "parameter.h"
#include "pending_writes.h"
#include "computed_parameter.h"
#include "encoder_bind_types.h"

extern "C" {
    #include <aether_radio/x6100_control/control.h>
}

// Clamp a value into [lo, hi]. Used by the parameter validators so C-API / set()
// values cannot leave their documented range. Template on the value type so the
// same helper handles both int32_t and float parameters.
template <typename T>
static inline T clamp_val(T v, T lo, T hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

class SettingsManager {
public:
    // Deferred-write buffer (PendingWrites). Declared BEFORE the parameters
    // (C++ member initialisation order) because every Parameter enqueues its
    // changes into this buffer via a WriteSink& bound at construction time.
    // Public because it is the shared sink of the managed parameters;
    // flushing goes through the manager methods (flush_all / flush_storage).
    PendingWrites pending_writes_;

    // Parameter registries (non-owning). MUST be declared before any Parameter
    // member — self-registration in NSDMI constructors pushes into these
    // vectors, so they must already exist (C++ member initialisation order).
    std::vector<ParamBase*> global_params_;
    std::vector<ParamBase*> band_params_;
    std::vector<ParamBase*> mode_params_;

    // -- Parameters (public for C-API / UI access; live for the whole program) --
    // All regular GLOBAL/BAND/MODE params are declared with NSDMI here (one
    // line each — the single source of truth). The trailing group argument
    // (&global_params_ / &band_params_ / &mode_params_) makes each Parameter
    // self-register into its registry on construction. VFO params and computed
    // params are deliberately NOT registered (loaded in dependency order by
    // load_band_vfo).

    // General
    Parameter<int32_t> p_volume{"volume", 30, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 100); }, {}, &global_params_};
    Parameter<float, int32_t, 10> p_pwr{"pwr", 5.0f, StorageType::GLOBAL, pending_writes_,
        [](float v) { return clamp_val(v, 0.1f, 10.0f); }, {}, &global_params_};
    Parameter<int32_t> p_squelch{"squelch", 0, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 100); }, {}, &global_params_};
    Parameter<int32_t> p_rfgain{"rfgain", 63, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 100); }, {}, &global_params_};

    Parameter<int32_t> p_rit{"rit", 0, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, -5000, 5000); }, {}, &global_params_};
    Parameter<int32_t> p_xit{"xit", 0, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, -5000, 5000); }, {}, &global_params_};
    // Current band id, persisted in the global `params` table (mirrors the
    // legacy cfg.band_id). Restored at init_load to pick the starting band;
    // updated on every band switch so a restart returns to the last band.
    // Default 7 = 20m SSB, matching the VFOA default frequency of 14.1 MHz.
    Parameter<int32_t> p_band_id{"band_id", 7, StorageType::GLOBAL, pending_writes_,
        {}, {}, &global_params_};
    Parameter<int32_t> p_mic{"mic", x6100_mic_auto, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_hmic{"hmic", 20, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_imic{"imic", 30, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_moni{"moni", 30, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_ant_id{"ant", 1, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_atu_enabled{"atu", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};

    // UI
    Parameter<int32_t> p_auto_level_enabled{"auto_level_enabled", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<float, int32_t, 2> p_auto_level_offset{"auto_level_offset", 0.0f, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_knob_info{"knob_info", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    // String mapping encoder (cfg_ctrl_t) positions to bind values
    // (EB_BIND_VOL/EB_BIND_MFK/EB_BIND_NONE). GLOBAL: persisted in the flat `params` table under key
    // "encoder_bind", matching the legacy src/cfg key for DB compatibility.
    Parameter<std::string> p_encoder_bind{"encoder_bind", make_default_encoder_bind(),
        StorageType::GLOBAL, pending_writes_,
        [](const std::string& val) -> std::string {
            std::string result = val;
            if (result.size() != static_cast<size_t>(EB_CTRL_FAST_ACCESS_LAST))
                result.resize(EB_CTRL_FAST_ACCESS_LAST, static_cast<char>(EB_BIND_NONE));
            return result;
        }, {}, &global_params_};

    // VOX
    Parameter<int32_t> p_vox_en{"vox_en", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_vox_gain{"vox_gain", 0, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_vox_ag{"vox_ag", 0, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_vox_delay{"vox_delay", 500, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};


    // FT8
    Parameter<int32_t> p_ft8_show_all{"ft8_show_all", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_ft8_protocol{"ft8_protocol", FTX_PROTOCOL_FT8, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_ft8_auto{"ft8_auto", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_ft8_hold_freq{"ft8_hold_freq", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_ft8_max_repeats{"ft8_max_repeats", 6, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};

    // SWR scan
    Parameter<int32_t> p_swrscan_linear{"swrscan_linear", true, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_swrscan_span{"swrscan_span", 200000, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};

    // CW
    Parameter<int32_t> p_key_tone{"key_tone", 700, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 400, 1400); }, {}, &global_params_};
    Parameter<int32_t> p_key_speed{"key_speed", 15, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 1, 60); }, {}, &global_params_};
    Parameter<int32_t> p_key_mode{"key_mode", x6100_key_manual, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_iambic_mode{"iambic_mode", x6100_iambic_a, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_key_vol{"key_vol", 10, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 100); }, {}, &global_params_};
    Parameter<int32_t> p_key_train{"key_train", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_qsk_time{"qsk_time", 100, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 1000); }, {}, &global_params_};
    Parameter<float, int32_t, 10> p_key_ratio{"key_ratio", 3.0f, StorageType::GLOBAL, pending_writes_,
        [](float v) { return clamp_val(v, 2.5f, 4.0f); }, {}, &global_params_};
    Parameter<int32_t> p_cw_peak_on{"cw_peak_on", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_cw_peak_q{"cw_peak_q", 1, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 10); }, {}, &global_params_};

    // CW decoder
    Parameter<int32_t> p_cw_decoder{"cw_decoder", true, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 1); }, {}, &global_params_};
    Parameter<int32_t> p_cw_tune{"cw_tune", true, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 1); }, {}, &global_params_};
    Parameter<float, int32_t, 10> p_cw_decoder_snr{"cw_decoder_snr_2", 5.0f, StorageType::GLOBAL, pending_writes_,
        [](float v) { return clamp_val(v, 0.0f, 30.0f); }, {}, &global_params_};
    Parameter<float, int32_t, 10> p_cw_decoder_snr_gist{"cw_decoder_snr_gist", 1.0f, StorageType::GLOBAL, pending_writes_,
        [](float v) { return clamp_val(v, 0.0f, 30.0f); }, {}, &global_params_};

    // AGC
    Parameter<int32_t> p_agc_hang{"agc_hang", false, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 1); }, {}, &global_params_};
    Parameter<int32_t> p_agc_knee{"agc_knee", -60, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, -120, 0); }, {}, &global_params_};
    Parameter<int32_t> p_agc_slope{"agc_slope", 6, StorageType::GLOBAL, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 20); }, {}, &global_params_};

    // DSP
    Parameter<int32_t> p_dnf{"dnf", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_dnf_center{"dnf_center", 1000, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_dnf_width{"dnf_width", 50, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_dnf_auto{"dnf_auto", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_nb{"nb", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_nb_level{"nb_level", 10, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_nb_width{"nb_width", 10, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_nr{"nr", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_nr_level{"nr_level", 0, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};

    // DSP custom
    Parameter<float, int32_t, 5> p_output_gain{"output_gain", 0.0f, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_comp{"comp", 4, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<float, int32_t, 2> p_comp_threshold_offset{"comp_threshold_offset", 0.0f, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<float, int32_t, 2> p_comp_makeup_offset{"comp_makeup_offset", 0.0f, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_fm_emphasis{"fm_emphasis", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_tx_filter_low{"tx_filter_low", 160, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_tx_filter_high{"tx_filter_high", 3000, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<int32_t> p_cessb_on{"cessb_on", false, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};
    Parameter<float, int32_t, 10> p_cessb_power_up{"cessb_power_up", 3.7f, StorageType::GLOBAL, pending_writes_,
        nullptr, {}, &global_params_};

    // Band params
    Parameter<int32_t> p_band_current_vfo{"vfo", X6100_VFO_A, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_if_shift{"if_shift", 0, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_grid_min{"grid_min", -121, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_grid_max{"grid_max", -73, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_split{"split", false, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_tx_i_offset{"tx_i_offset", 0, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    Parameter<int32_t> p_band_tx_q_offset{"tx_q_offset", 0, StorageType::BAND, pending_writes_,
        nullptr, {}, &band_params_};
    // dB offset of the front panel audio chain (float, scaled x10 to an int
    // in the DB: 0.2 dB <-> 2).
    Parameter<float, int32_t, 10> p_band_dac_offset{"dac_offset", 0.0f, StorageType::BAND, pending_writes_,
        {}, {}, &band_params_};
    // BAND VFO params (context_id = bands_id). These are NOT in the band
    // registry: load_band_vfo() loads them in explicit dependency order so the
    // DB restore/clamp rules can reference already-loaded siblings.
    Parameter<int32_t> p_band_vfoa_freq{"vfoa_freq", 14100000, StorageType::BAND, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 60000000); }};
    Parameter<int32_t> p_band_vfob_freq{"vfob_freq", 14150000, StorageType::BAND, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 60000000); }};
    Parameter<int32_t> p_band_vfoa_mode{"vfoa_mode", x6100_mode_usb, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfob_mode{"vfob_mode", x6100_mode_usb, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfoa_att{"vfoa_att", x6100_att_off, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfoa_pre{"vfoa_pre", x6100_pre_off, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfoa_agc{"vfoa_agc", x6100_agc_auto, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfob_att{"vfob_att", x6100_att_off, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfob_pre{"vfob_pre", x6100_pre_off, StorageType::BAND, pending_writes_};
    Parameter<int32_t> p_band_vfob_agc{"vfob_agc", x6100_agc_auto, StorageType::BAND, pending_writes_};

    // Mode params
    Parameter<int32_t> p_mode_squelch{"squelch", 0, StorageType::MODE, pending_writes_,
        [](int32_t v) { return clamp_val(v, 0, 100); }, {}, &mode_params_};
    Parameter<int32_t> p_mode_agc{"agc", x6100_agc_auto, StorageType::MODE, pending_writes_,
        nullptr, {}, &mode_params_};
    Parameter<int32_t> p_mode_freq_step{"freq_step", 500, StorageType::MODE, pending_writes_,
        [](int32_t v) { return clamp_val(v, 1, 10000); }, {}, &mode_params_};
    Parameter<int32_t> p_mode_spectrum_factor{"spectrum_factor", 1, StorageType::MODE, pending_writes_,
        [](int32_t v) { return clamp_val(v, 1, 8); }, {}, &mode_params_};

private:
    // MODE-scoped filter band edges. PRIVATE: never read/written directly
    // outside this class; external C++ reads the effective values via the
    // public computed params cp_cur_filter_low/high/bw and mutates them
    // through their set() (reverse path). Still self-register into
    // mode_params_ so load_mode_all/switch_mode load them like any mode param.
    // Cross-validators reference the sibling member; they only run on set /
    // set_quiet (never during construction), by which time both exist.
    Parameter<int32_t> p_mode_filter_low{"filter_low", 50, StorageType::MODE, pending_writes_,
        [this](int32_t v) {
            int other = p_mode_filter_high.get();
            int hi = other - 1;
            if (hi < 0) hi = 0;
            return clamp_val(v, 0, hi);
        }, {}, &mode_params_};
    Parameter<int32_t> p_mode_filter_high{"filter_high", 2900, StorageType::MODE, pending_writes_,
        [this](int32_t v) {
            int other = clamp_val(p_mode_filter_low.get(), 0, 5999);
            return clamp_val(v, other + 1, 6000);
        }, {}, &mode_params_};

public:

    // Computed parameter: front-panel ("FG") frequency = the frequency of the
    // active VFO of the current band. Constructed with compute/reverse closures
    // in the SettingsManager ctor (no default ctor exists).
    ComputedParameter<int32_t> cp_fg_freq;

    // Computed parameter: the mode of the active VFO of the current band.
    // Analogous to cp_fg_freq: p_band_current_vfo selects between the VFO A/B
    // mode params. Constructed with compute/reverse closures in the ctor.
    ComputedParameter<int32_t> cp_cur_mode;

    // Computed parameters for the current VFO's att/pre/agc and the background
    // VFO frequency. Att/pre/agc track the ACTIVE VFO's value; bg_freq returns
    // the INACTIVE VFO's frequency. Constructed with closures in the ctor,
    // sources bound in init_load().
    ComputedParameter<int32_t> cp_cur_att;
    ComputedParameter<int32_t> cp_cur_pre;
    ComputedParameter<int32_t> cp_cur_agc;
    ComputedParameter<int32_t> cp_bg_freq;

    // Computed current filter params for the active MODE category. The compute
    // fns map the MODE-scoped filter_low/filter_high (and key_tone for CW)
    // through the mode category (SSB/AM/FM/CW); the reverse fns write back into
    // them. Modelled on cp_fg_freq/cp_cur_mode: constructed with closures in the
    // ctor (no default ctor), sources bound in init_load().
    ComputedParameter<int32_t> cp_cur_filter_low;
    ComputedParameter<int32_t> cp_cur_filter_high;
    ComputedParameter<int32_t> cp_cur_filter_bw;

    // -- Construction / lifetime --
    explicit SettingsManager();
    ~SettingsManager();

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    SettingsManager(SettingsManager&&) = delete;
    SettingsManager& operator=(SettingsManager&&) = delete;

    // -- Initialisation --
    // Loads global params (including the persisted band_id), then band params
    // for the restored band and mode params for the mode derived from the
    // active VFO (cp_cur_mode). on_db_error (optional) is invoked on DB
    // failures; parameters keep their current values on NOT_FOUND.
    void init_load(void (*on_db_error)(const char* msg) = nullptr);

    // -- Context switching --
    // Switches mode context: saves pending mode writes and loads mode params
    // for new_mode_id.
    void switch_mode(int new_mode_id);

    // -- PendingWrites --
    void flush_all();
    void flush_storage(StorageType type, int context_id);

    // -- VFO operations --
    // Copies VFO-related band parameters (freq, mode, agc, att, pre) from the
    // active VFO to the inactive VFO, based on the current p_band_current_vfo.
    void cfg_band_vfo_copy();

    // Background deferred-save thread: wakes every 3 seconds (or on notify)
    // and calls flush_all().
    //
    // The flush thread must satisfy PendingWrites invariants I1/I2 (see
    // pending_writes.h): it only reads the queue's own snapshots + the
    // stateless policies and never touches SettingsManager member state
    // (band_id_, mode_id_, the parameters), so it cannot race manager context
    // or param mutation.
    void start_flush_thread();
    void stop_flush_thread();

    // -- Accessors --
    int current_band_id() const { return band_id_; }
    int current_mode_id() const { return mode_id_; }

private:
    // active VFO frequency of the current band (compute fn for cp_fg_freq).
    int32_t fg_freq_get();

    // reverse fn for cp_fg_freq: writes freq into the active VFO param.
    void fg_freq_set(int32_t freq);

    // compute/reverse fns for cp_bg_freq: the background (inactive) VFO's
    // frequency, i.e. the complement of the active VFO.
    int32_t bg_freq_get();
    void bg_freq_set(int32_t freq);

    // active VFO mode of the current band (compute fn for cp_cur_mode).
    int32_t cur_mode_get();

    // reverse fn for cp_cur_mode: writes mode into the active VFO's mode param.
    void cur_mode_set(int32_t mode);

    // compute/reverse fns for cp_cur_att / cp_cur_pre / cp_cur_agc: read/write
    // the active VFO's att/pre/agc param based on p_band_current_vfo.
    int32_t cur_att_get();
    void cur_att_set(int32_t att);
    int32_t cur_pre_get();
    void cur_pre_set(int32_t pre);
    int32_t cur_agc_get();
    void cur_agc_set(int32_t agc);


    // Trampoline for the cp_cur_mode observer: calls switch_mode() with the new
    // mode whenever cp_cur_mode's value changes.
    static void switch_mode_observer_cb(appcfg::Subject* subj, void* user_data);

    // Trampoline for the p_band_vfoa_freq / p_band_vfob_freq observers: when a
    // VFO frequency changes into a different band, triggers an implicit band
    // switch (the frequency that caused the switch is preserved).
    static void vfo_freq_change_cb(appcfg::Subject* subj, void* user_data);

    // Trampoline for the p_band_id observer: an explicit band switch is triggered
    // by setting p_band_id; the observer runs switch_band with implicit=false.
    static void switch_band_observer_cb(appcfg::Subject* subj, void* user_data);

    // Shared band-switch core: flush, rebind context, switch-time loads and
    // recomputes. `implicit` selects whether the active VFO's freq+mode is kept.
    void switch_band(int new_band_id, bool implicit);

    // Apply the current band/mode context to every parameter in the group.
    void set_band_context(int band_id);
    void set_mode_context(int mode_id);

    // Full group loads (used by init_load).
    void load_band_all(int band_id);
    void load_mode_all(int mode_id);

    // Switch-time band loads: never loads current_vfo; when `implicit` is true
    // also keeps the active VFO's freq+mode (loads only the inactive VFO).
    void load_band_switch(int new_band_id, bool implicit);

    // Loads the four VFO params (vfoa_freq, vfoa_mode, vfob_freq, vfob_mode) in
    // explicit dependency order, applying the DB restore/clamp rules:
    //   - vfoa_freq: NOT_FOUND -> BandInfo.start_freq (or 12000000 if the band
    //     is undefined/missing); a loaded value outside [start,stop] is clamped
    //     to start_freq.
    //   - vfoa_mode: NOT_FOUND/error -> resolve_default_mode(vfoa_freq).
    //   - vfob_freq: NOT_FOUND -> current vfoa_freq; else clamped as vfoa_freq.
    //   - vfob_mode: NOT_FOUND/error -> current vfoa_mode.
    // Restored/clamped values are applied with set_quiet (no deferred write).
    // When `implicit` the active VFO's freq+mode is skipped (kept, not loaded).
    void load_band_vfo(int band_id, bool implicit);

    // Default mode for a frequency: LSB below 10 MHz, USB at or above.
    static int32_t resolve_default_mode(int32_t freq);

    // Default 49-char encoder-bind string (all NONE except the documented
    // VOL/MFK positions). Static: callable from the ctor init list.
    static std::string make_default_encoder_bind();

    // Filter category of a mode (SSB/AM/FM/CW; unknown/default -> SSB).
    enum class FilterMode { SSB, AM, FM, CW };
    FilterMode filter_mode(int32_t mode) const;

    // compute fns for cp_cur_filter_{low,high,bw}.
    int32_t cur_filter_low_compute();
    int32_t cur_filter_high_compute();
    int32_t cur_filter_bw_compute();

    // reverse fns for cp_cur_filter_{low,high,bw}: map back into
    // p_mode_filter_low/high per the current mode category.
    void cur_filter_low_reverse(int32_t v);
    void cur_filter_high_reverse(int32_t v);
    void cur_filter_bw_reverse(int32_t v);

    // -- Member data --
    int band_id_ = 0;
    int mode_id_ = 0;

    // Re-entrancy guard for switch_band: prevents the p_band_id observer (which
    // fires from inside switch_band's p_band_id.set) from running a nested
    // switch_band with a different `implicit` flag.
    bool band_switch_active_ = false;

    // Holds the cp_cur_mode observer that triggers switch_mode() on mode change.
    // RAII: unsubscribes on destruction.
    appcfg::Subscription switch_mode_obs_;

    // Holds the p_band_id observer that triggers an explicit band switch when
    // p_band_id is set. RAII: unsubscribe on destruction.
    appcfg::Subscription band_id_obs_;

    // Holds the VFO frequency observers that trigger an implicit band switch
    // when the active VFO is tuned into a different band. RAII: unsubscribe on
    // destruction.
    appcfg::Subscription vfoa_freq_obs_;
    appcfg::Subscription vfob_freq_obs_;

    // Background flush thread control.
    bool flush_thread_running_ = false;
    std::thread flush_thread_;
    std::condition_variable flush_cv_;
    std::mutex flush_mutex_;
};

// Global SettingsManager instance (defined in cfg_api.cpp). Accessed directly
// by C++ code (e.g. cat.cpp) without going through the C API layer.
extern SettingsManager g_cfg;
