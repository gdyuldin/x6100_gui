#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void lm_set_ab(bool val);
void lm_set_mode(bool val);
void lm_set_freq(bool val);
void lm_set_band(bool val);

void lm_toggle_ab(void);
void lm_toggle_mode(void);
void lm_toggle_freq(void);
void lm_toggle_band(void);

bool lm_get_ab(void);
bool lm_get_mode(void);
bool lm_get_freq(void);
bool lm_get_band(void);

#ifdef __cplusplus
}
#endif
