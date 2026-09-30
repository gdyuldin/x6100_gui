// cfg_capi.cpp
// C-linkage wrappers for the cfg module: generic Subject helpers, Parameter<T>
// and ComputedParameter<T> typed access, the ATU tuner-network cache and the
// SettingsManager-level operations. The accessor tree and cfg_api_init() live
// in cfg_api.cpp; everything else declared in cfg_api.h is defined here.

#include "cfg_api.h"

#include <cstddef>
#include <cstdio>
#include <string>

#include "atu.h"
#include "db.h"
#include "settings_internal.h"

// ---------------------------------------------------------------------------
// Generic subject helpers and the shared observer release path
// ---------------------------------------------------------------------------

void param_unsubscribe(Observer *o) {
    if (!o) {
        return;
    }
    o->unsubscribe();
}

SubjectInt *subject_i_create(int32_t val) {
    return new SubjectInt(val);
}

int32_t subject_i_get(SubjectInt *subj) {
    return subj->get();
}

void subject_i_set(SubjectInt *subj, int32_t val) {
    subj->set(val);
}

SubjectFloat *subject_f_create(float val) {
    return new SubjectFloat(val);
}

float subject_f_get(SubjectFloat *subj) {
    return subj->get();
}

void subject_f_set(SubjectFloat *subj, float val) {
    subj->set(val);
}

Observer *subject_subscribe(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe(fn, user_data);
}

Observer *subject_subscribe_and_notify(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_and_notify(fn, user_data);
}

ObserverDelayed *subject_subscribe_delayed(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed(fn, user_data);
}

ObserverDelayed *subject_subscribe_delayed_and_notify(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed_and_notify(fn, user_data);
}

void observer_delayed_drain(void) {
    ObserverDelayed::drain();
}

void observer_delayed_shutdown(void) {
    ObserverDelayed::shutdown();
}

// ---------------------------------------------------------------------------
// Parameter<T> typed access
// ---------------------------------------------------------------------------

int32_t param_i_get(const ParamInt *p) {
    return p->get();
}

void param_i_set(ParamInt *p, int32_t v) {
    p->set(v);
}

float param_f_get(const ParamFloat *p) {
    return p->get();
}

void param_f_set(ParamFloat *p, float v) {
    p->set(v);
}

char *param_t_get_into(const ParamText *p, char *dst, size_t n) {
    if (!dst || n == 0) {
        return dst;
    }
    std::string value = p->get();
    snprintf(dst, n, "%s", value.c_str());
    return dst;
}

void param_t_set(ParamText *p, const char *v) {
    std::string sv = v ? v : "";
    p->set(sv);
}

// ---------------------------------------------------------------------------
// ComputedParameter<T> typed access
// ---------------------------------------------------------------------------

void cparam_i_set(ComputedParamInt *p, int32_t value) {
    p->set(value);
}

int32_t cparam_i_get(const ComputedParamInt *p) {
    return p->get();
}

void cparam_f_set(ComputedParamFloat *p, float value) {
    p->set(value);
}

float cparam_f_get(const ComputedParamFloat *p) {
    return p->get();
}

void cparam_t_set(ComputedParamText *p, const char *value) {
    std::string sv = value ? value : "";
    p->set(sv);
}

// ---------------------------------------------------------------------------
// ATU tuner-network cache
// ---------------------------------------------------------------------------

int cfg_atu_save_network(uint32_t network) {
    return atu_network.save_network(cfg_sm.p_ant_id.get(), cfg_sm.cp_fg_freq.get(), network);
}

bool cfg_atu_is_loaded(void) {
    return atu_network.loaded.get();
}

uint32_t cfg_atu_get_network(void) {
    return atu_network.network.get();
}

Observer *cfg_atu_network_subscribe(observer_cb cb, void *user_data) {
    return atu_network.network.subscribe(cb, user_data);
}

// ---------------------------------------------------------------------------
// SettingsManager-level operations
// ---------------------------------------------------------------------------

void cfg_init(void) {
    if (!cfg_db_open(CFG_DB_PATH)) {
        LV_LOG_ERROR("Can't initialise settings database");
        return;
    }

    // Prepare the table statements against the freshly opened connection.
    cfg_db_init(cfg_db_get());

    // Load global/band/mode params into the SettingsManager.
    cfg_api_init(NULL);

    // Background deferred-save thread for the managed params.
    cfg_api_start_flush_thread();
}

int32_t cfg_transverter_shift_for(int32_t freq) {
    return cfg_sm.transverter_shift_for(freq);
}

bool cfg_is_valid_hw_freq(int32_t freq) {
    return cfg_sm.is_valid_hw_freq(freq);
}

void cfg_api_flush_all(void) {
    cfg_sm.flush_all();
}

void cfg_api_start_flush_thread(void) {
    cfg_sm.start_flush_thread();
}

void cfg_api_stop_flush_thread(void) {
    cfg_sm.stop_flush_thread();
}

void cfg_band_load_next(bool up) {
    int32_t            cur_freq = cfg_sm.cp_fg_freq.get();
    int32_t            cur_id   = cfg_sm.current_band_id();
    BandInfoLoadResult result   = BandsTable::next(cur_id, static_cast<uint32_t>(cur_freq), up);
    if (result.rc == SUCCESS) {
        cfg_sm.p_band_id.set(result.value.id);
    }
}

void cfg_band_vfo_copy(void) {
    cfg_sm.cfg_band_vfo_copy();
}

int32_t cfg_mode_change_freq_step(bool up) {
    static const uint16_t steps[] = {10, 100, 500, 1000, 5000};
    static const size_t   n       = sizeof(steps) / sizeof(steps[0]);
    int32_t               step    = cfg_sm.p_mode_freq_step.get();
    size_t                i;
    for (i = 0; i < n; i++)
        if (step == static_cast<int32_t>(steps[i]))
            break;
    i    = (i + (up ? 1 : -1) + n) % n;
    step = steps[i];
    cfg_sm.p_mode_freq_step.set(step);
    return step;
}
