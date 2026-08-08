#include "settings_manager.h"

#include <chrono>
#include <cstdio>

extern "C" {
    #include <aether_radio/x6100_control/control.h>
}

// SettingsManager implementation.
//
// Band/mode switching semantics (aligned with src/cfg/band.c):
//   - switch_band_explicit: the user explicitly selected a new band. Save the
//     pending writes of the current band, then load all band params of the new
//     band EXCEPT current_vfo (the active VFO reference stays the same VFO).
//   - switch_band_implicit: the radio crossed into a new band while tuning.
//     Same as explicit, but additionally the frequency of the active VFO is
//     left untouched (it keeps the current tune frequency).
//   - switch_mode: the user changed the mode. Save pending mode writes and
//     load the mode params for the new mode.
//
// All loads use Parameter::load(), which reads the context_id bound via
// set_context_id() — no context threading through every call.

SettingsManager::SettingsManager()
    : // Computed front-panel frequency: the frequency of the active VFO of the
      // current band. The compute function reads the band VFO params; the
      // reverse function writes back into the active VFO's frequency param.
      // Sources (VFO frequency params + current_vfo) are bound once in
      // init_load() via ComputedParameter::bind().
      cp_fg_freq(
          [this]() { return fg_freq_from_band(); },
          [this](int32_t freq) { fg_freq_to_band(freq); }),

      // Computed current mode: the mode of the active VFO of the current band.
      // Analogous to cp_fg_freq. Sources (VFO mode params + current_vfo) are
      // bound once in init_load(); an observer on this subject triggers
      // switch_mode() whenever the current mode changes.
      cp_cur_mode(
          [this]() { return get_cur_mode(); },
          [this](int32_t mode) { set_cur_mode(mode); }),

      // Computed current filter params. The compute fns derive the effective
      // filter edges/bw from the MODE-scoped filter params (and key_tone for
      // CW) according to the active mode's category; the reverse fns write
      // back into filter_low/filter_high. Sources are bound in init_load().
      cp_cur_filter_low(
          [this]() { return cur_filter_low_compute(); },
          [this](int32_t v) { cur_filter_low_reverse(v); }),
      cp_cur_filter_high(
          [this]() { return cur_filter_high_compute(); },
          [this](int32_t v) { cur_filter_high_reverse(v); }),
      cp_cur_filter_bw(
          [this]() { return cur_filter_bw_compute(); },
          [this](int32_t v) { cur_filter_bw_reverse(v); }),

      // Computed current VFO's att/pre/agc. The compute fns read the active
      // VFO's value; the reverse fns write back into it. The background VFO
      // frequency is the complement of the active one.
      cp_cur_att(
          [this]() { return get_cur_att(); },
          [this](int32_t att) { set_cur_att(att); }),
      cp_cur_pre(
          [this]() { return get_cur_pre(); },
          [this](int32_t pre) { set_cur_pre(pre); }),
      cp_cur_agc(
          [this]() { return get_cur_agc(); },
          [this](int32_t agc) { set_cur_agc(agc); }),
      cp_bg_freq(
          [this]() { return bg_freq_from_band(); },
          [this](int32_t freq) { bg_freq_to_band(freq); })
{
    // All Parameter members self-register via NSDMI in the header — no
    // registration calls needed here. The constructor is empty.
}

SettingsManager::~SettingsManager()
{
    stop_flush_thread();
}

void SettingsManager::init_load(int band_id, int mode_id, void (*on_db_error)(const char* msg))
{
    // Global params (flat `params` table; context_id is ignored). The unified
    // ParamBase registry now includes p_pwr (float) and p_encoder_bind (text),
    // so the non-int32 special cases no longer need explicit load calls.
    for (ParamBase* p : global_params_)
    {
        int rc = p->load(0);
        if (rc != SUCCESS && rc != NOT_FOUND && on_db_error)
        {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Failed to load %s", p->db_name());
            on_db_error(msg);
        }
    }

    band_id_ = band_id;
    mode_id_ = mode_id;

    set_band_context(band_id_);
    set_mode_context(mode_id_);

    load_band_all(band_id_);
    load_mode_all(mode_id_);

    // Bind the computed fg_freq to its sources (VFO frequency params +
    // current_vfo) so it recomputes automatically when they change.
    cp_fg_freq.bind(p_band_vfoa_freq);
    cp_fg_freq.bind(p_band_vfob_freq);
    cp_fg_freq.bind(p_band_current_vfo);

    // Recompute fg_freq from the freshly loaded band params.
    cp_fg_freq.recompute();

    // Bind the computed cur_mode to its sources (VFO mode params + current_vfo)
    // and recompute it BEFORE subscribing the switch_mode observer, so the
    // initial recompute does not fire switch_mode().
    cp_cur_mode.bind(p_band_vfoa_mode);
    cp_cur_mode.bind(p_band_vfob_mode);
    cp_cur_mode.bind(p_band_current_vfo);
    cp_cur_mode.recompute();

    // Subscribe an observer that calls switch_mode() whenever the current mode
    // changes (e.g. from cp_cur_mode.set() or a VFO switch).
    switch_mode_obs_ = Subscription(cp_cur_mode.subscribe(switch_mode_observer_cb, this));

    // Bind the computed current VFO att/pre/agc and the background VFO
    // frequency to their sources and recompute from the freshly loaded params.
    cp_cur_att.bind(p_band_vfoa_att);
    cp_cur_att.bind(p_band_vfob_att);
    cp_cur_att.bind(p_band_current_vfo);
    cp_cur_att.recompute();

    cp_cur_pre.bind(p_band_vfoa_pre);
    cp_cur_pre.bind(p_band_vfob_pre);
    cp_cur_pre.bind(p_band_current_vfo);
    cp_cur_pre.recompute();

    cp_cur_agc.bind(p_band_vfoa_agc);
    cp_cur_agc.bind(p_band_vfob_agc);
    cp_cur_agc.bind(p_band_current_vfo);
    cp_cur_agc.recompute();

    cp_bg_freq.bind(p_band_vfoa_freq);
    cp_bg_freq.bind(p_band_vfob_freq);
    cp_bg_freq.bind(p_band_current_vfo);
    cp_bg_freq.recompute();

    // Bind the computed filter params to their sources (filter_low/high +
    // key_tone) so reverse writes to them propagate to the sibling cur_* and
    // direct p_mode_filter_*.set() keeps cur_* in sync. Recompute after the
    // mode params have been loaded.
    cp_cur_filter_low.bind(p_mode_filter_low);
    cp_cur_filter_low.bind(p_mode_filter_high);
    cp_cur_filter_low.bind(p_key_tone);
    cp_cur_filter_high.bind(p_mode_filter_low);
    cp_cur_filter_high.bind(p_mode_filter_high);
    cp_cur_filter_high.bind(p_key_tone);
    cp_cur_filter_bw.bind(cp_cur_filter_low);
    cp_cur_filter_bw.bind(cp_cur_filter_high);
    cp_cur_filter_low.recompute();
    cp_cur_filter_high.recompute();
    cp_cur_filter_bw.recompute();
}

void SettingsManager::switch_band_explicit(int new_band_id)
{
    switch_band(new_band_id, false);
}

void SettingsManager::switch_band_implicit(int new_band_id)
{
    switch_band(new_band_id, true);
}

void SettingsManager::switch_band(int new_band_id, bool implicit)
{
    // Save pending writes of the current band before switching context.
    pending_writes_.flush_storage(StorageType::BAND, band_id_);

    band_id_ = new_band_id;

    set_band_context(band_id_);

    load_band_switch(band_id_, implicit);

    cp_fg_freq.recompute();
    cp_cur_mode.recompute();
    cp_cur_att.recompute();
    cp_cur_pre.recompute();
    cp_cur_agc.recompute();
    cp_bg_freq.recompute();
}

void SettingsManager::switch_mode(int new_mode_id)
{
    pending_writes_.flush_storage(StorageType::MODE, mode_id_);

    mode_id_ = new_mode_id;

    set_mode_context(mode_id_);

    load_mode_all(mode_id_);

    // The mode context can affect computed parameters that depend on mode
    // (e.g. current_mode_id); recompute the fg frequency chain.
    cp_fg_freq.recompute();

    // Mode loads use set_quiet (no source notify), and the category may have
    // flipped, so recompute the filter chain explicitly. NOT bound to
    // cp_cur_mode: binding would recompute with stale filter values at the
    // moment the mode flips.
    cp_cur_filter_low.recompute();
    cp_cur_filter_high.recompute();
    cp_cur_filter_bw.recompute();
}

void SettingsManager::flush_all()
{
    pending_writes_.flush_all();
}

void SettingsManager::flush_storage(StorageType type, int context_id)
{
    pending_writes_.flush_storage(type, context_id);
}

void SettingsManager::cfg_band_vfo_copy()
{
    if (p_band_current_vfo.get() == X6100_VFO_A)
    {
        p_band_vfob_freq.set(p_band_vfoa_freq.get());
        p_band_vfob_mode.set(p_band_vfoa_mode.get());
        p_band_vfob_agc.set(p_band_vfoa_agc.get());
        p_band_vfob_att.set(p_band_vfoa_att.get());
        p_band_vfob_pre.set(p_band_vfoa_pre.get());
    }
    else
    {
        p_band_vfoa_freq.set(p_band_vfob_freq.get());
        p_band_vfoa_mode.set(p_band_vfob_mode.get());
        p_band_vfoa_agc.set(p_band_vfob_agc.get());
        p_band_vfoa_att.set(p_band_vfob_att.get());
        p_band_vfoa_pre.set(p_band_vfob_pre.get());
    }
}

void SettingsManager::start_flush_thread()
{
    if (flush_thread_running_) {
        return;
    }
    flush_thread_running_ = true;
    flush_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(flush_mutex_);
        while (flush_thread_running_) {
            // Wake every 3 seconds (or on notify) and persist pending changes.
            flush_cv_.wait_for(lock, std::chrono::seconds(3), [this]() { return !flush_thread_running_; });
            if (!flush_thread_running_) {
                break;
            }
            pending_writes_.flush_all();
        }
    });
}

void SettingsManager::stop_flush_thread()
{
    if (!flush_thread_running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(flush_mutex_);
        flush_thread_running_ = false;
    }
    flush_cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

int32_t SettingsManager::fg_freq_from_band()
{
    // Front-panel frequency = frequency of the active VFO of the current band.
    return p_band_current_vfo.get() == 0 ? p_band_vfoa_freq.get() : p_band_vfob_freq.get();
}

void SettingsManager::fg_freq_to_band(int32_t freq)
{
    // Write back into the active VFO's frequency param.
    if (p_band_current_vfo.get() == 0) {
        p_band_vfoa_freq.set(freq);
    } else {
        p_band_vfob_freq.set(freq);
    }
}

int32_t SettingsManager::get_cur_mode()
{
    // Current mode = mode of the active VFO of the current band.
    return p_band_current_vfo.get() == 0 ? p_band_vfoa_mode.get() : p_band_vfob_mode.get();
}

void SettingsManager::set_cur_mode(int32_t mode)
{
    // Write back into the active VFO's mode param.
    if (p_band_current_vfo.get() == 0) {
        p_band_vfoa_mode.set(mode);
    } else {
        p_band_vfob_mode.set(mode);
    }
}

int32_t SettingsManager::get_cur_att()
{
    return p_band_current_vfo.get() == X6100_VFO_A ? p_band_vfoa_att.get() : p_band_vfob_att.get();
}

void SettingsManager::set_cur_att(int32_t att)
{
    if (p_band_current_vfo.get() == X6100_VFO_A) {
        p_band_vfoa_att.set(att);
    } else {
        p_band_vfob_att.set(att);
    }
}

int32_t SettingsManager::get_cur_pre()
{
    return p_band_current_vfo.get() == X6100_VFO_A ? p_band_vfoa_pre.get() : p_band_vfob_pre.get();
}

void SettingsManager::set_cur_pre(int32_t pre)
{
    if (p_band_current_vfo.get() == X6100_VFO_A) {
        p_band_vfoa_pre.set(pre);
    } else {
        p_band_vfob_pre.set(pre);
    }
}

int32_t SettingsManager::get_cur_agc()
{
    return p_band_current_vfo.get() == X6100_VFO_A ? p_band_vfoa_agc.get() : p_band_vfob_agc.get();
}

void SettingsManager::set_cur_agc(int32_t agc)
{
    if (p_band_current_vfo.get() == X6100_VFO_A) {
        p_band_vfoa_agc.set(agc);
    } else {
        p_band_vfob_agc.set(agc);
    }
}

int32_t SettingsManager::bg_freq_from_band()
{
    return p_band_current_vfo.get() == X6100_VFO_A ? p_band_vfob_freq.get() : p_band_vfoa_freq.get();
}

void SettingsManager::bg_freq_to_band(int32_t freq)
{
    if (p_band_current_vfo.get() == X6100_VFO_A) {
        p_band_vfob_freq.set(freq);
    } else {
        p_band_vfoa_freq.set(freq);
    }
}

SettingsManager::FilterMode SettingsManager::filter_mode(int32_t mode) const
{
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_usb:
        case x6100_mode_usb_dig:
            return FilterMode::SSB;
        case x6100_mode_am:
            return FilterMode::AM;
        case x6100_mode_nfm:
            return FilterMode::FM;
        case x6100_mode_cw:
        case x6100_mode_cwr:
            return FilterMode::CW;
        default:
            return FilterMode::SSB;
    }
}

int32_t SettingsManager::cur_filter_low_compute()
{
    // Current mode category drives the edge mapping. The filter params are
    // MODE-scoped to cp_cur_mode.get() (matches mode_id_ at steady state).
    int32_t low;
    switch (filter_mode(cp_cur_mode.get())) {
        case FilterMode::AM:
        case FilterMode::FM:
            return 0;
        case FilterMode::CW:
            // Low can't be negative
            low = p_key_tone.get() - p_mode_filter_high.get() / 2;
            return LV_MAX(0, low);
        case FilterMode::SSB:
        default:
            return p_mode_filter_low.get();
    }
}

int32_t SettingsManager::cur_filter_high_compute()
{
    int32_t low;
    switch (filter_mode(cp_cur_mode.get())) {
        case FilterMode::SSB:
        case FilterMode::AM:
        case FilterMode::FM:
        default:
            return p_mode_filter_high.get();
        case FilterMode::CW:
            low = p_key_tone.get() - p_mode_filter_high.get() / 2;
            low = LV_MAX(0, low);
            return low + p_mode_filter_high.get();
    }
}

int32_t SettingsManager::cur_filter_bw_compute()
{
    return cp_cur_filter_high.get() - cp_cur_filter_low.get();
}

void SettingsManager::cur_filter_low_reverse(int32_t v)
{
    switch (filter_mode(cp_cur_mode.get())) {
        case FilterMode::SSB:
            p_mode_filter_low.set(v);
            break;
        case FilterMode::AM:
        case FilterMode::FM:
            // no-op: low is always 0 for AM/FM.
            break;
        case FilterMode::CW:
            // filter_high is centred on key_tone: low = key_tone - high/2.
            p_mode_filter_high.set(2 * (p_key_tone.get() - v));
            break;
    }
}

void SettingsManager::cur_filter_high_reverse(int32_t v)
{
    switch (filter_mode(cp_cur_mode.get())) {
        case FilterMode::SSB:
        case FilterMode::AM:
        case FilterMode::FM:
            p_mode_filter_high.set(v);
            break;
        case FilterMode::CW:
            p_mode_filter_high.set(2 * (v - p_key_tone.get()));
            break;
    }
}

void SettingsManager::cur_filter_bw_reverse(int32_t v)
{
    v = clamp_val(v, 10, 6000);
    switch (filter_mode(cp_cur_mode.get())) {
        case FilterMode::AM:
        case FilterMode::FM:
            // bw == filter_high in AM/FM (low is 0).
            p_mode_filter_high.set(v);
            break;
        case FilterMode::SSB: {
            const int32_t mid = (p_mode_filter_low.get() + p_mode_filter_high.get()) / 2;
            int32_t low = mid - v / 2;
            int32_t high = mid + v / 2;
            if (low < 0) {
                low = 0;
                high = v;
            }
            p_mode_filter_low.set(low);
            p_mode_filter_high.set(high);
            break;
        }
        case FilterMode::CW:
            // bw == filter_high in CW (the +/- offset cancels).
            p_mode_filter_high.set(v);
            break;
    }
}

void SettingsManager::switch_mode_observer_cb(Subject* /*subj*/, void* user_data)
{
    // Whenever the current mode changes, switch the mode context so the
    // MODE-scoped params track the active VFO's mode. switch_mode() only loads
    // MODE params (set_quiet) and recomputes cp_fg_freq, never cp_cur_mode, so
    // this does not recurse.
    SettingsManager* mgr = static_cast<SettingsManager*>(user_data);
    mgr->switch_mode(mgr->cp_cur_mode.get());
}

void SettingsManager::set_band_context(int band_id)
{
    // Unified band registry now includes p_band_dac_offset (float), so the
    // explicit set_context_id call is no longer needed.
    for (ParamBase* p : band_params_)
    {
        p->set_context_id(band_id);
    }
    // The four VFO params are not in the generic registry (see load_band_vfo),
    // but they still need the band context so their deferred writes target the
    // right band.
    p_band_vfoa_freq.set_context_id(band_id);
    p_band_vfob_freq.set_context_id(band_id);
    p_band_vfoa_mode.set_context_id(band_id);
    p_band_vfob_mode.set_context_id(band_id);
    p_band_vfoa_att.set_context_id(band_id);
    p_band_vfob_att.set_context_id(band_id);
    p_band_vfoa_pre.set_context_id(band_id);
    p_band_vfob_pre.set_context_id(band_id);
    p_band_vfoa_agc.set_context_id(band_id);
    p_band_vfob_agc.set_context_id(band_id);
}

void SettingsManager::set_mode_context(int mode_id)
{
    for (ParamBase* p : mode_params_)
    {
        p->set_context_id(mode_id);
    }
}

void SettingsManager::load_band_all(int band_id)
{
    for (ParamBase* p : band_params_)
    {
        p->load(band_id);
    }

    // VFO params are handled in explicit order (restore/clamp with sibling
    // dependency). Initial full load restores nothing (no implicit skip).
    load_band_vfo(band_id, false);
}

void SettingsManager::load_mode_all(int mode_id)
{
    for (ParamBase* p : mode_params_)
    {
        p->load(mode_id);
    }
}

void SettingsManager::load_band_switch(int new_band_id, bool implicit)
{
    // A switch never reloads current_vfo (the active VFO reference stays). The
    // generic registry holds current_vfo + if_shift + dac_offset; this loop
    // loads if_shift and dac_offset, skipping current_vfo. The VFO params
    // (and the implicit active-VFO skip) are handled by load_band_vfo.
    for (ParamBase* p : band_params_)
    {
        if (p == &p_band_current_vfo)
        {
            continue;
        }
        p->load(new_band_id);
    }
    load_band_vfo(new_band_id, implicit);
}

void SettingsManager::load_band_vfo(int band_id, bool implicit)
{
    const int active_vfo = p_band_current_vfo.get();

    // BandInfo drives the clamp/restore rules. A missing or undefined band
    // disables clamping; vfoa_freq then falls back to a default restore.
    const int32_t default_freq = 12000000;
    BandInfoLoadResult band = BandsTable::get_by_id(band_id);
    const bool have_band = (band.rc == SUCCESS) && (band.value.id != BAND_UNDEFINED);

    // vfoa_freq: NOT_FOUND -> band start (or the default); an out-of-range
    // loaded value is clamped to the band start.
    if (!(implicit && active_vfo == X6100_VFO_A)) {
        int rc = p_band_vfoa_freq.load(band_id);
        if (rc == NOT_FOUND) {
            p_band_vfoa_freq.set_quiet(have_band ? static_cast<int32_t>(band.value.start_freq) : default_freq);
        } else if (rc == SUCCESS && have_band) {
            const int32_t freq = p_band_vfoa_freq.get();
            if (freq < static_cast<int32_t>(band.value.start_freq) ||
                freq > static_cast<int32_t>(band.value.stop_freq)) {
                p_band_vfoa_freq.set_quiet(static_cast<int32_t>(band.value.start_freq));
            }
        }
    }

    // vfoa_mode: depends on the (loaded/restored) vfoa_freq.
    if (!(implicit && active_vfo == X6100_VFO_A)) {
        int rc = p_band_vfoa_mode.load(band_id);
        if (rc != SUCCESS) {
            p_band_vfoa_mode.set_quiet(resolve_default_mode(p_band_vfoa_freq.get()));
        }
    }

    // vfob_freq: copies the current vfoa_freq when absent; clamped like
    // vfoa_freq when loaded.
    if (!(implicit && active_vfo == X6100_VFO_B)) {
        int rc = p_band_vfob_freq.load(band_id);
        if (rc == NOT_FOUND) {
            p_band_vfob_freq.set_quiet(p_band_vfoa_freq.get());
        } else if (rc == SUCCESS && have_band) {
            const int32_t freq = p_band_vfob_freq.get();
            if (freq < static_cast<int32_t>(band.value.start_freq) ||
                freq > static_cast<int32_t>(band.value.stop_freq)) {
                p_band_vfob_freq.set_quiet(static_cast<int32_t>(band.value.start_freq));
            }
        }
    }

    // vfob_mode: copies the current vfoa_mode when absent.
    if (!(implicit && active_vfo == X6100_VFO_B)) {
        int rc = p_band_vfob_mode.load(band_id);
        if (rc != SUCCESS) {
            p_band_vfob_mode.set_quiet(p_band_vfoa_mode.get());
        }
    }

    // vfoa_att/pre/agc: on NOT_FOUND keep the default silently (no restore).
    if (!(implicit && active_vfo == X6100_VFO_A)) {
        int rc = p_band_vfoa_att.load(band_id);
        (void)rc;
    }
    if (!(implicit && active_vfo == X6100_VFO_A)) {
        int rc = p_band_vfoa_pre.load(band_id);
        (void)rc;
    }
    if (!(implicit && active_vfo == X6100_VFO_A)) {
        int rc = p_band_vfoa_agc.load(band_id);
        (void)rc;
    }

    // vfob_att/pre/agc: on NOT_FOUND copy the current vfoa value.
    if (!(implicit && active_vfo == X6100_VFO_B)) {
        int rc = p_band_vfob_att.load(band_id);
        if (rc == NOT_FOUND) {
            p_band_vfob_att.set_quiet(p_band_vfoa_att.get());
        }
    }
    if (!(implicit && active_vfo == X6100_VFO_B)) {
        int rc = p_band_vfob_pre.load(band_id);
        if (rc == NOT_FOUND) {
            p_band_vfob_pre.set_quiet(p_band_vfoa_pre.get());
        }
    }
    if (!(implicit && active_vfo == X6100_VFO_B)) {
        int rc = p_band_vfob_agc.load(band_id);
        if (rc == NOT_FOUND) {
            p_band_vfob_agc.set_quiet(p_band_vfoa_agc.get());
        }
    }
}

int32_t SettingsManager::resolve_default_mode(int32_t freq)
{
    return freq < 10000000 ? x6100_mode_lsb : x6100_mode_usb;
}

std::string SettingsManager::make_default_encoder_bind()
{
    std::string s(EB_CTRL_FAST_ACCESS_LAST, static_cast<char>(EB_BIND_NONE));

    s[EB_CTRL_VOL]             = static_cast<char>(EB_BIND_VOL);
    s[EB_CTRL_RFG]             = static_cast<char>(EB_BIND_VOL);
    s[EB_CTRL_FILTER_LOW]      = static_cast<char>(EB_BIND_VOL);
    s[EB_CTRL_FILTER_HIGH]     = static_cast<char>(EB_BIND_VOL);
    s[EB_CTRL_PWR]             = static_cast<char>(EB_BIND_VOL);
    s[EB_CTRL_HMIC]            = static_cast<char>(EB_BIND_VOL);

    s[EB_CTRL_SPECTRUM_FACTOR] = static_cast<char>(EB_BIND_MFK);
    s[EB_CTRL_DNF]             = static_cast<char>(EB_BIND_MFK);
    s[EB_CTRL_AGC_KNEE]        = static_cast<char>(EB_BIND_MFK);

    return s;
}
