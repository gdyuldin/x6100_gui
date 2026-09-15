#pragma once

// Audio playback port. Implemented by the application (audio.c) and injected
// so consumers do not include audio.h. The player handle is the same opaque
// struct audio_player_s that audio.h forward-declares.

#include <stddef.h>
#include <stdint.h>

typedef struct audio_player_s audio_port_player_t;

typedef struct {
    audio_port_player_t *(*get_player)(uint32_t sample_rate, uint32_t ch);
    audio_port_player_t *(*create_player)(uint32_t sample_rate, uint32_t ch);
    int  (*player_send)(audio_port_player_t *player, int16_t *samples, size_t n);
    void (*player_wait)(audio_port_player_t *player);
    void (*player_release)(audio_port_player_t *player);
    float (*set_play_vol)(float db);
    void (*gain_db)(int16_t *buf, size_t samples, float gain, int16_t *out);
    void (*gain_db_transition)(int16_t *buf, size_t samples, float gain1, float gain2, int16_t *out);
} audio_port_t;
