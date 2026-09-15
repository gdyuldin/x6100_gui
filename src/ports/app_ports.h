#pragma once

// Aggregated application ports. The application implements the concrete ports
// (see app_ports.h / app_ports.cpp) and injects this struct into the libraries
// at init (cat_init, cat_lan_init, tx_worker_construct). Tests build their own
// instance with fakes.

#include "audio_port.h"
#include "dsp_audio_port.h"
#include "psd_port.h"
#include "radio_port.h"
#include "telemetry_port.h"

typedef struct {
    const radio_port_t     *radio;
    const telemetry_port_t *telemetry;
    const audio_port_t     *audio;
    const dsp_audio_port_t *dsp_audio;
    const psd_port_t       *psd;
} app_ports_t;
