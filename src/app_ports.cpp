/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Production implementations of the application ports. These adapt the real
 *  application services (radio, telemetry, audio, DSP) to the low-level port
 *  interfaces in src/ports/, so libraries never include the application
 *  headers.
 */

#include "app_ports.h"

// radio.h pulls cfg/subject_api.h, whose C++ build includes subject.h with the
// C++ templates; pull it before the extern "C" block so those types are not
// parsed with C linkage.
#include "cfg/subject_api.h"
#include "dsp.h"

extern "C" {
#include "audio.h"
#include "radio.h"
#include "tx_info.h"
}

/* Radio */

static bool port_is_rx(void) { return radio_get_state() == RADIO_RX; }
static void port_set_ptt(bool on) { radio_set_ptt(on); }
static void port_set_freq(int32_t freq) { radio_set_freq(freq); }
static void port_set_modem(bool on) { radio_set_modem(on); }
static void port_set_pwr(float w) { radio_set_pwr(w); }

/* Telemetry */

static bool port_tx_info_refresh(uint8_t *prev_msg_id, float *alc, float *pwr, float *vswr) {
    return tx_info_refresh(prev_msg_id, alc, pwr, vswr);
}

static float port_s_meter_get_raw_db(void) { return dsp_get_s_meter_db(); }

/* Audio */

static audio_port_player_t *port_audio_get_player(uint32_t rate, uint32_t ch) {
    return reinterpret_cast<audio_port_player_t *>(audio_get_player(rate, ch));
}

static audio_port_player_t *port_audio_create_player(uint32_t rate, uint32_t ch) {
    return reinterpret_cast<audio_port_player_t *>(audio_create_player(rate, ch));
}

static int port_audio_player_send(audio_port_player_t *player, int16_t *samples, size_t n) {
    return audio_player_send(reinterpret_cast<audio_player_t *>(player), samples, n);
}

static void port_audio_player_wait(audio_port_player_t *player) {
    audio_player_wait(reinterpret_cast<audio_player_t *>(player));
}

static void port_audio_player_release(audio_port_player_t *player) {
    audio_player_release(reinterpret_cast<audio_player_t *>(player));
}

static float port_audio_set_play_vol(float db) { return audio_set_play_vol(db); }

static void port_audio_gain_db(int16_t *buf, size_t samples, float gain, int16_t *out) {
    audio_gain_db(buf, samples, gain, out);
}

static void port_audio_gain_db_transition(int16_t *buf, size_t samples, float gain1, float gain2, int16_t *out) {
    audio_gain_db_transition(buf, samples, gain1, gain2, out);
}

/* DSP audio */

static uint32_t port_dsp_audio_subscribe_float(dsp_audio_float_cb_t cb, uint32_t rate) {
    return dsp_audio_subscribe_float(cb, rate);
}

static void port_dsp_audio_set_active(uint32_t id, bool active) { dsp_audio_set_active(id, active); }
static void port_dsp_audio_unsubscribe(uint32_t id) { dsp_audio_unsubscribe(id); }

/* PSD source (CI-V scope streaming) */

#define MAX_PSD_SUBS 4

struct PsdSlot {
    uint32_t        dsp_id;
    psd_frame_cb_t  cb;
    void           *ud;
};

static PsdSlot psd_slots[MAX_PSD_SUBS];

static void psd_adapter(const dsp_frame_t *frame, void *user_data) {
    PsdSlot *slot = static_cast<PsdSlot *>(user_data);

    if (slot->cb) {
        slot->cb(frame->psd_db, frame->size, frame->base_freq, frame->width_hz, frame->min, frame->max, slot->ud);
    }
}

static uint32_t port_psd_subscribe(psd_frame_cb_t cb, uint16_t nfft, uint16_t chunks_per_frame, void *user_data) {
    if (!cb) {
        return PSD_SUB_INVALID;
    }

    for (size_t i = 0; i < MAX_PSD_SUBS; i++) {
        if (psd_slots[i].dsp_id != PSD_SUB_INVALID) {
            continue;
        }

        const dsp_frame_cfg_t sub_cfg = {
            .nfft             = nfft,
            .chunks_per_frame = chunks_per_frame,
            .allow_vary_freq  = false,
        };
        uint32_t id = dsp_frame_subscribe(&sub_cfg, &psd_adapter, &psd_slots[i]);
        if (id == DSP_FRAME_SUB_INVALID) {
            return PSD_SUB_INVALID;
        }

        psd_slots[i].dsp_id = id;
        psd_slots[i].cb     = cb;
        psd_slots[i].ud     = user_data;
        return id;
    }

    return PSD_SUB_INVALID;
}

static void port_psd_set_active(uint32_t id, bool active) { dsp_frame_set_active(id, active); }

static void port_psd_set_chunks_per_frame(uint32_t id, uint16_t chunks_per_frame) {
    dsp_frame_set_chunks_per_frame(id, chunks_per_frame);
}

static void port_psd_unsubscribe(uint32_t id) {
    if (id == PSD_SUB_INVALID) {
        return;
    }

    dsp_frame_unsubscribe(id);

    for (size_t i = 0; i < MAX_PSD_SUBS; i++) {
        if (psd_slots[i].dsp_id == id) {
            psd_slots[i].dsp_id = PSD_SUB_INVALID;
            psd_slots[i].cb     = nullptr;
            psd_slots[i].ud     = nullptr;
            return;
        }
    }
}

static const radio_port_t radio_port = {
    .is_rx = &port_is_rx,
    .set_ptt = &port_set_ptt,
    .set_freq = &port_set_freq,
    .set_modem = &port_set_modem,
    .set_pwr = &port_set_pwr,
};

static const telemetry_port_t telemetry_port = {
    .tx_info_refresh = &port_tx_info_refresh,
    .s_meter_get_raw_db = &port_s_meter_get_raw_db,
};

static const audio_port_t audio_port = {
    .get_player = &port_audio_get_player,
    .create_player = &port_audio_create_player,
    .player_send = &port_audio_player_send,
    .player_wait = &port_audio_player_wait,
    .player_release = &port_audio_player_release,
    .set_play_vol = &port_audio_set_play_vol,
    .gain_db = &port_audio_gain_db,
    .gain_db_transition = &port_audio_gain_db_transition,
};

static const dsp_audio_port_t dsp_audio_port = {
    .subscribe_float = &port_dsp_audio_subscribe_float,
    .set_active = &port_dsp_audio_set_active,
    .unsubscribe = &port_dsp_audio_unsubscribe,
};

static const psd_port_t psd_port = {
    .subscribe = &port_psd_subscribe,
    .set_active = &port_psd_set_active,
    .set_chunks_per_frame = &port_psd_set_chunks_per_frame,
    .unsubscribe = &port_psd_unsubscribe,
};

const app_ports_t app_ports = {
    .radio = &radio_port,
    .telemetry = &telemetry_port,
    .audio = &audio_port,
    .dsp_audio = &dsp_audio_port,
    .psd = &psd_port,
};
