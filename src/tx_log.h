#pragma once

#include <stdbool.h>
#include <stdint.h>

void tx_log_init(void);
void tx_log_event(const char *event, int32_t freq_hz, int32_t mode, float pwr, const char *detail);
