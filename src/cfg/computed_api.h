#pragma once

// C-compatible API for computed parameters. Only set/subscribe are provided:
// creation stays C++-only (ComputeFn/ReverseFn are C++ callables, constructed
// by SettingsManager); typed get/set arrive through cfg_api.h.
//
// Usage from C code:
//   extern ComputedParamInt* cfg_fg_freq;   // filled by C++ side
//   cparam_i_set(cfg_fg_freq, 7100000);

#include <stdint.h>

#ifdef __cplusplus
#include "computed_parameter.h"
using ComputedParamInt   = ComputedParameter<int32_t>;
using ComputedParamFloat = ComputedParameter<float>;
using ComputedParamText  = ComputedParameter<std::string>;
#else
typedef struct ComputedParamInt   ComputedParamInt;
typedef struct ComputedParamFloat ComputedParamFloat;
typedef struct ComputedParamText  ComputedParamText;
#endif

#ifdef __cplusplus
extern "C" {
#endif

void    cparam_i_set(ComputedParamInt *p, int32_t value);
int32_t cparam_i_get(const ComputedParamInt *p);

void  cparam_f_set(ComputedParamFloat *p, float value);
float cparam_f_get(const ComputedParamFloat *p);

void cparam_t_set(ComputedParamText *p, const char *value);

#ifdef __cplusplus
} // extern "C"
#endif
