#include "cfg_api.h"

#include <string>

#include "settings_manager.h"
#include "computed_parameter.h"
#include "subject.h"

// Subject/Observer types live in namespace appcfg (see subject.h); the C-API
// layer is extern "C" global, so pull the names in for this TU only.
using namespace appcfg;

// The opaque C handles are the concrete Parameter<T> / ComputedParameter<T>
// instantiations owned by the static SettingsManager below. Casts are safe
// because C code only ever receives them through this API.

// Owned by C++ for the whole program; never deleted.
SettingsManager g_cfg;

// --- extern globals (declared in cfg_api.h), filled by cfg_api_init() ---
// Regular global/band/mode params; special params (VFO freq, scaled float,
// computed) stay explicit.
ParamInt*   cfg_volume          = nullptr;
ParamInt*   cfg_squelch         = nullptr;
ParamInt*   cfg_rfgain          = nullptr;
ParamInt*   cfg_rit             = nullptr;
ParamInt*   cfg_xit             = nullptr;
ParamFloat* cfg_pwr             = nullptr;
ParamInt*   cfg_band_current_vfo = nullptr;
ParamInt*   cfg_band_if_shift   = nullptr;
ParamInt*   cfg_mode_squelch    = nullptr;
ParamInt*   cfg_mode_agc        = nullptr;
ParamInt*        cfg_band_vfoa_freq  = nullptr;
ParamInt*        cfg_band_vfob_freq  = nullptr;
ParamFloat*      cfg_band_dac_offset = nullptr;
ComputedParamInt* cfg_fg_freq       = nullptr;
ParamInt*   cfg_transverter_0_from  = nullptr;
ParamInt*   cfg_transverter_0_to    = nullptr;
ParamInt*   cfg_transverter_0_shift = nullptr;
ParamInt*   cfg_transverter_1_from  = nullptr;
ParamInt*   cfg_transverter_1_to    = nullptr;
ParamInt*   cfg_transverter_1_shift = nullptr;

void cfg_api_init(void (*on_db_error)(const char*))
{
    // on_db_error is kept for signature compatibility but is currently inert
    // (plan A.1 dropped the wiring): the manager stores it but never invokes it.
    // The starting band/mode are derived internally (persisted global band_id +
    // cp_cur_mode).
    g_cfg.init_load(on_db_error);

    cfg_volume           = reinterpret_cast<ParamInt*>(&g_cfg.p_volume);
    cfg_squelch          = reinterpret_cast<ParamInt*>(&g_cfg.p_squelch);
    cfg_rfgain           = reinterpret_cast<ParamInt*>(&g_cfg.p_rfgain);
    cfg_rit              = reinterpret_cast<ParamInt*>(&g_cfg.p_rit);
    cfg_xit              = reinterpret_cast<ParamInt*>(&g_cfg.p_xit);
    cfg_pwr              = reinterpret_cast<ParamFloat*>(&g_cfg.p_pwr);
    cfg_band_current_vfo = reinterpret_cast<ParamInt*>(&g_cfg.p_band_current_vfo);
    cfg_band_if_shift    = reinterpret_cast<ParamInt*>(&g_cfg.p_band_if_shift);
    cfg_mode_squelch     = reinterpret_cast<ParamInt*>(&g_cfg.p_mode_squelch);
    cfg_mode_agc         = reinterpret_cast<ParamInt*>(&g_cfg.p_mode_agc);
    cfg_band_vfoa_freq   = reinterpret_cast<ParamInt*>(&g_cfg.p_band_vfoa_freq);
    cfg_band_vfob_freq   = reinterpret_cast<ParamInt*>(&g_cfg.p_band_vfob_freq);
    cfg_band_dac_offset  = reinterpret_cast<ParamFloat*>(&g_cfg.p_band_dac_offset);
    cfg_fg_freq          = reinterpret_cast<ComputedParamInt*>(&g_cfg.cp_fg_freq);

    cfg_transverter_0_from  = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_0_from);
    cfg_transverter_0_to    = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_0_to);
    cfg_transverter_0_shift = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_0_shift);
    cfg_transverter_1_from  = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_1_from);
    cfg_transverter_1_to    = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_1_to);
    cfg_transverter_1_shift = reinterpret_cast<ParamInt*>(&g_cfg.p_transverter_1_shift);
}

int32_t cfg_transverter_shift_for(int32_t freq)
{
    return g_cfg.transverter_shift_for(freq);
}

bool cfg_is_valid_hw_freq(int32_t freq)
{
    return g_cfg.is_valid_hw_freq(freq);
}

void cfg_api_flush_all(void)
{
    g_cfg.flush_all();
}

// --- Typed accessors ---

int32_t param_int_get(const ParamInt* p)
{
    return reinterpret_cast<const Parameter<int32_t>*>(p)->get();
}

void param_int_set(ParamInt* p, int32_t v)
{
    reinterpret_cast<Parameter<int32_t>*>(p)->set(v);
}

float param_float_get(const ParamFloat* p)
{
    return reinterpret_cast<const Parameter<float>*>(p)->get();
}

void param_float_set(ParamFloat* p, float v)
{
    reinterpret_cast<Parameter<float>*>(p)->set(v);
}

const char* param_text_get(const ParamText* p)
{
    // Borrowed pointer: valid until the parameter's next set().
    return reinterpret_cast<const Parameter<std::string>*>(p)->get().c_str();
}

void param_text_set(ParamText* p, const char* v)
{
    // NULL is treated as an empty string (never dereference NULL).
    std::string sv = v ? v : "";
    reinterpret_cast<Parameter<std::string>*>(p)->set(sv);
}

// --- Subscription trampolines ---
//
// Each typed C callback needs a tiny adapter holding {opaque param, C callback,
// user_data}. The adapter is allocated per subscription and stored as the
// observer's user_data; the trampoline casts it back and forwards the call.
// A common virtual base lets param_unsubscribe delete any adapter via the
// observer's user_data without knowing its concrete type.
namespace {

struct OpaqueObserverAdapter {
    virtual ~OpaqueObserverAdapter() = default;
};

struct IntAdapter : OpaqueObserverAdapter {
    IntAdapter(ParamInt* p, param_int_cb c, void* u) : param(p), cb(c), user(u) {}
    ParamInt* param;
    param_int_cb cb;
    void* user;
};

struct FloatAdapter : OpaqueObserverAdapter {
    FloatAdapter(ParamFloat* p, param_float_cb c, void* u) : param(p), cb(c), user(u) {}
    ParamFloat* param;
    param_float_cb cb;
    void* user;
};

struct TextAdapter : OpaqueObserverAdapter {
    TextAdapter(ParamText* p, param_text_cb c, void* u) : param(p), cb(c), user(u) {}
    ParamText* param;
    param_text_cb cb;
    void* user;
};

void int_cb_trampoline(Subject* /*subj*/, void* user_data)
{
    auto* a = static_cast<IntAdapter*>(user_data);
    a->cb(a->param, a->user);
}

void float_cb_trampoline(Subject* /*subj*/, void* user_data)
{
    auto* a = static_cast<FloatAdapter*>(user_data);
    a->cb(a->param, a->user);
}

void text_cb_trampoline(Subject* /*subj*/, void* user_data)
{
    auto* a = static_cast<TextAdapter*>(user_data);
    a->cb(a->param, a->user);
}

} // namespace

Observer* param_int_subscribe(ParamInt* p, param_int_cb cb, void* user_data)
{
    auto* adapter = new IntAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<int32_t>*>(p);
    return subj->subscribe(int_cb_trampoline, adapter);
}

ObserverDelayed* param_int_subscribe_delayed(ParamInt* p, param_int_cb cb, void* user_data)
{
    auto* adapter = new IntAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<int32_t>*>(p);
    return subj->subscribe_delayed(int_cb_trampoline, adapter);
}

Observer* param_float_subscribe(ParamFloat* p, param_float_cb cb, void* user_data)
{
    auto* adapter = new FloatAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<float>*>(p);
    return subj->subscribe(float_cb_trampoline, adapter);
}

ObserverDelayed* param_float_subscribe_delayed(ParamFloat* p, param_float_cb cb, void* user_data)
{
    auto* adapter = new FloatAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<float>*>(p);
    return subj->subscribe_delayed(float_cb_trampoline, adapter);
}

Observer* param_text_subscribe(ParamText* p, param_text_cb cb, void* user_data)
{
    auto* adapter = new TextAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<std::string>*>(p);
    return subj->subscribe(text_cb_trampoline, adapter);
}

ObserverDelayed* param_text_subscribe_delayed(ParamText* p, param_text_cb cb, void* user_data)
{
    auto* adapter = new TextAdapter{p, cb, user_data};
    auto* subj = reinterpret_cast<Parameter<std::string>*>(p);
    return subj->subscribe_delayed(text_cb_trampoline, adapter);
}

void param_unsubscribe(Observer* o)
{
    if (!o) {
        return;
    }
    // Free the per-subscription adapter, then unsubscribe + delete the observer
    // (ObserverDeleter). Must be called on the main thread for delayed
    // observers (consistent with the ObserverDelayed lifetime contract).
    delete static_cast<OpaqueObserverAdapter*>(o->get_user_data());
    ObserverDeleter{}(o);
}
