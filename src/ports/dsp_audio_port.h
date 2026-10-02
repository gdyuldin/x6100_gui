#pragma once

// DSP audio subscription port. Implemented by the application (dsp.cpp) and
// injected so consumers do not include dsp.h.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*dsp_audio_float_cb_t)(size_t n, float *samples);

// Subscription id that is never returned by subscribe_float().
#define DSP_AUDIO_SUB_INVALID (0u)

typedef struct {
    uint32_t (*subscribe_float)(dsp_audio_float_cb_t cb, uint32_t target_rate_hz);
    void     (*set_active)(uint32_t id, bool active);
    void     (*unsubscribe)(uint32_t id);
} dsp_audio_port_t;
