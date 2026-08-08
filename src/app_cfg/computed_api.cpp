#include "computed_api.h"
#include "computed_parameter.h"

#include <cstdint>
#include <string>

// The opaque pointers are the concrete ComputedParameter<T> instantiations
// created and owned by the C++ side (SettingsManager). C code only ever
// receives them through the API below, so casts are safe.

void computed_param_int_set(ComputedParamInt* p, int32_t value) {
    reinterpret_cast<ComputedParameter<int32_t>*>(p)->set(value);
}

int32_t computed_param_int_get(const ComputedParamInt* p) {
    return reinterpret_cast<const ComputedParameter<int32_t>*>(p)->get();
}

void computed_param_float_set(ComputedParamFloat* p, float value) {
    reinterpret_cast<ComputedParameter<float>*>(p)->set(value);
}

float computed_param_float_get(const ComputedParamFloat* p) {
    return reinterpret_cast<const ComputedParameter<float>*>(p)->get();
}

void computed_param_text_set(ComputedParamText* p, const char* value) {
    // NULL is treated as an empty string (never dereference NULL).
    std::string sv = value ? value : "";
    reinterpret_cast<ComputedParameter<std::string>*>(p)->set(sv);
}
