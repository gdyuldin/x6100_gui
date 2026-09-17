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
    /* size is the number of bins requested by the subscriber (no default).
     * chunks_per_frame is the DSP chunk cadence requested by the subscriber
     * (counted in BASE chunks, not milliseconds; 0 is rejected). */
    uint32_t (*subscribe)(psd_frame_cb_t cb, uint16_t size, uint16_t chunks_per_frame, void *user_data);
    void     (*set_active)(uint32_t id, bool active);
    void     (*set_chunks_per_frame)(uint32_t id, uint16_t chunks_per_frame);
    void     (*unsubscribe)(uint32_t id);
} psd_port_t;
