#pragma once

// C-compatible API for computed parameters. These are the sizes of int32_t,
// float and std::string specializations of ComputedParameter<T>, exposed to C
// code as opaque pointers. Only set/get are provided on purpose: creation
// stays C++-only (ComputeFn/ReverseFn are C++ callables, constructed by
// SettingsManager); a full typed C API (param_int_get/set/subscribe) arrives
// with cfg_api.h in a later step.
//
// Usage from C code:
//   extern ComputedParamInt* cfg_fg_freq;   // filled by C++ side
//   computed_param_int_set(cfg_fg_freq, 7100000);

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ComputedParamInt ComputedParamInt;     // = ComputedParameter<int32_t>
typedef struct ComputedParamFloat ComputedParamFloat; // = ComputedParameter<float>
typedef struct ComputedParamText ComputedParamText;   // = ComputedParameter<std::string>

void    computed_param_int_set(ComputedParamInt* p, int32_t value);
int32_t computed_param_int_get(const ComputedParamInt* p);

void  computed_param_float_set(ComputedParamFloat* p, float value);
float computed_param_float_get(const ComputedParamFloat* p);

void computed_param_text_set(ComputedParamText* p, const char* value);

#ifdef __cplusplus
} // extern "C"
#endif
