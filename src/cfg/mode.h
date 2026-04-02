#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_FILTER_FREQ 10000

int32_t cfg_mode_change_freq_step(bool up);
int32_t cfg_mode_set_low_filter(int32_t val);
int32_t cfg_mode_set_high_filter(int32_t val);


#ifdef __cplusplus
}
#endif
