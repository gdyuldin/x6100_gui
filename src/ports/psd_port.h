#pragma once

// PSD frame source port. Implemented by the application over the DSP frame
// subscriptions (dsp_frame_subscribe), so CAT/scope code can receive spectrum
// frames without including dsp.h.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PSD values are in dB, already offset and level-resolved by the DSP.
typedef void (*psd_frame_cb_t)(const float *psd_db, size_t size,
                               uint32_t base_freq, uint32_t width_hz,
                               float min, float max, void *user_data);

#define PSD_SUB_INVALID (0u)

typedef struct {
    uint32_t (*subscribe)(psd_frame_cb_t cb, void *user_data);
    void     (*set_active)(uint32_t id, bool active);
    void     (*unsubscribe)(uint32_t id);
} psd_port_t;
