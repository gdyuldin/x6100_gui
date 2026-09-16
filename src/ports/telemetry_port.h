#pragma once

// TX telemetry port (tx_info + meter). Implemented by the application and
// injected so consumers do not include tx_info.h / meter.h.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // Refresh TX telemetry. Returns true when new data was available.
    bool (*tx_info_refresh)(uint8_t *prev_msg_id, float *alc, float *pwr, float *vswr);
    // Current S-meter reading in raw dB units.
    float (*s_meter_get_raw_db)(void);
} telemetry_port_t;
