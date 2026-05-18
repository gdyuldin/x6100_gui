/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "worker.h"

#include "../util.h"
#include "gfsk.h"

#include "lvgl/lvgl.h"
#include <ft8lib/constants.h>
#include <ft8lib/decode.h>
#include <ft8lib/encode.h>
#include <ft8lib/hashtable.h>
#include <ft8lib/message.h>
#include <liquid/liquid.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

#define MAX_CANDIDATES 200
#define MAX_DECODED_MESSAGES 50
#define LDPC_ITERATIONS 25
#define TIME_OSR 4               // Time oversampling rate (symbol subdivision)
#define FREQ_OSR 2               // Frequency oversampling rate (bin subdivision)
#define FTXLIB_MIN_SCORE_DEFAULT 10
#define FTXLIB_MIN_SCORE_PATH "/mnt/ft8lib_min_score.txt"
#define DECODE_BLOCK_STRIDE 2    // Try to decode each N block
#define EARLY_LDPC_ITERATIONS 25 // LDPC iterations on early decoding

static float complex *time_buf;
static float complex *freq_buf;
static fftplan        fft;
static windowcf       frame_window;
static complex float *rx_window = NULL;

static float symbol_period;
static int   block_size;
static int   subblock_size;
static int   nfft;

static uint8_t n_tones;  // Number of tones for generate message and check minimal length for rx
static uint8_t sync_num; // Length of sync

static int             ft8lib_min_score = FTXLIB_MIN_SCORE_DEFAULT;

static void ftx_min_score_write_default(void) {
    FILE *wf = fopen(FTXLIB_MIN_SCORE_PATH, "w");
    if (!wf) return;
    fprintf(wf, "%d\n", FTXLIB_MIN_SCORE_DEFAULT);
    fclose(wf);
}

static void ftx_min_score_load_or_create(void) {
    FILE *f = fopen(FTXLIB_MIN_SCORE_PATH, "r");
    if (!f) {
        if (errno == ENOENT) {
            ftx_min_score_write_default();
        }
        ft8lib_min_score = FTXLIB_MIN_SCORE_DEFAULT;
        return;
    }
    char buf[64] = {0};
    bool ok = false;
    if (fgets(buf, sizeof(buf), f)) {
        char *end = NULL;
        errno = 0;
        long v = strtol(buf, &end, 10);
        if ((errno == 0) && (end != buf) && (v >= 0) && (v <= 32767)) {
            while (end && *end && isspace((unsigned char)*end)) end++;
            if ((end != NULL) && (*end == '\0')) {
                ft8lib_min_score = (int)v;
                ok = true;
            }
        }
    }
    fclose(f);
    if (!ok) {
        ft8lib_min_score = FTXLIB_MIN_SCORE_DEFAULT;
        ftx_min_score_write_default();
    }
}

static int             num_candidates;
static ftx_candidate_t candidate_list[MAX_CANDIDATES];
static ftx_message_t   decoded[MAX_DECODED_MESSAGES];
static ftx_message_t  *decoded_hashtable[MAX_DECODED_MESSAGES];
static ftx_waterfall_t wf;
static int             find_candidates_at;

static void decode_messages(const ftx_waterfall_t *wf, int *num_candidates, ftx_candidate_t *candidate_list,
                            ftx_message_t *decoded, ftx_message_t **decoded_hashtable, int ldpc_iterations,
                            decoded_msg_cb msg_cb, void *user_data);

static int get_message_snr(const ftx_waterfall_t *wf, const ftx_candidate_t *candidate, ftx_message_t *msg);

/**
 * Init worker
 */
void ftx_worker_init(int sample_rate, ftx_protocol_t protocol) {
    ftx_min_score_load_or_create();
    float slot_period;

    switch (protocol) {
    case FTX_PROTOCOL_FT8:
        slot_period = FT8_SLOT_TIME;
        symbol_period = FT8_SYMBOL_PERIOD;
        n_tones = FT8_NN;
        sync_num = FT8_NUM_SYNC;
        break;
    case FTX_PROTOCOL_FT4:
        slot_period = FT4_SLOT_TIME;
        symbol_period = FT4_SYMBOL_PERIOD;
        n_tones = FT4_NN;
        sync_num = FT4_NUM_SYNC;
        break;
    default:
        LV_LOG_ERROR("Unsupported protocol: %lu", protocol);
        return;
    }
    int num_samples = slot_period * sample_rate;

    hashtable_init(256);

    /* FT8 decoder */

    block_size = (int)(sample_rate * symbol_period); // samples corresponding to one FSK symbol
    subblock_size = block_size / TIME_OSR;

    const int max_blocks = (int)(slot_period / symbol_period);
    // TODO: skip bins outside filter
    const int num_bins = sample_rate * symbol_period / 2;

    /* Check for integer overflow before multiplication */
    if (max_blocks <= 0 || num_bins <= 0) {
        LV_LOG_ERROR("FT8 worker: invalid dimensions (max_blocks=%d, num_bins=%d)", max_blocks, num_bins);
        goto error_cleanup;
    }

    const size_t mag_stride = (size_t)TIME_OSR * (size_t)FREQ_OSR * (size_t)num_bins * sizeof(WF_ELEM_T);
    if (mag_stride == 0 || (size_t)max_blocks > SIZE_MAX / mag_stride) {
        LV_LOG_ERROR("FT8 worker: mag_size calculation overflow");
        goto error_cleanup;
    }

    size_t mag_size = (size_t)max_blocks * mag_stride;

    wf.max_blocks = max_blocks;
    wf.num_bins = num_bins;
    wf.time_osr = TIME_OSR;
    wf.freq_osr = FREQ_OSR;
    wf.block_stride = TIME_OSR * FREQ_OSR * num_bins;
    wf.mag = (uint8_t *)malloc(mag_size);
    if (!wf.mag) {
        LV_LOG_ERROR("FT8 worker: wf.mag malloc failed");
        goto error_cleanup;
    }
    wf.protocol = protocol;

    find_candidates_at = n_tones - sync_num;

    /* FT8 DSP */
    nfft = block_size * FREQ_OSR;
    float fft_norm = 2.0f / nfft;
    time_buf = (float complex *)malloc(nfft * sizeof(float complex));
    freq_buf = (float complex *)malloc(nfft * sizeof(float complex));
    if (!time_buf || !freq_buf) {
        LV_LOG_ERROR("FT8 worker: time_buf/freq_buf malloc failed");
        goto error_cleanup;
    }
    fft = fft_create_plan(nfft, time_buf, freq_buf, LIQUID_FFT_FORWARD, 0);
    frame_window = windowcf_create(nfft);

    rx_window = malloc(nfft * sizeof(complex float));
    if (!rx_window) {
        LV_LOG_ERROR("FT8 worker: rx_window malloc failed");
        goto error_cleanup;
    }
    float window_norm = 2.0f / nfft;

    for (uint16_t i = 0; i < nfft; i++) {
        rx_window[i] = liquid_hann(i, nfft) * window_norm;
    }

    ftx_worker_reset();
    return;

error_cleanup:
    ftx_worker_free();
}

/**
 * Cleanup worker
 */
void ftx_worker_free() {
    if (wf.mag) {
        free(wf.mag);
        wf.mag = NULL;
    }

    if (frame_window) {
        windowcf_destroy(frame_window);
        frame_window = NULL;
    }
    if (fft) {
        fft_destroy_plan(fft);
        fft = NULL;
    }
    if (time_buf) {
        free(time_buf);
        time_buf = NULL;
    }
    if (freq_buf) {
        free(freq_buf);
        freq_buf = NULL;
    }
    if (rx_window) {
        free(rx_window);
        rx_window = NULL;
    }
    hashtable_delete();
}

/**
 * Reset worker
 */
void ftx_worker_reset() {
    hashtable_cleanup(10);
    wf.num_blocks = 0;
    num_candidates = 0;
    // Initialize hash table pointers
    for (int i = 0; i < MAX_DECODED_MESSAGES; ++i) {
        decoded_hashtable[i] = NULL;
    }
}

bool ftx_worker_generate_tx_samples(const char *text, bool force_free_text, const uint16_t signal_freq,
                                    const uint32_t sample_rate, int16_t **samples, uint32_t *n_samples) {
    ftx_message_t    msg;
    ftx_message_init(&msg);
    ftx_message_rc_t rc;
    if (force_free_text) {
        rc = ftx_message_encode_free(&msg, text);
    } else {
        rc = ftx_message_encode(&msg, &hash_if, text);
    }

    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_ERROR("Cannot parse message %i", rc);
        return false;
    }

    uint8_t tones[n_tones];
    float   symbol_bt;

    switch (wf.protocol) {
    case FTX_PROTOCOL_FT8:
        ft8_encode(msg.payload, tones);
        symbol_bt = FT8_SYMBOL_BT;
        break;
    case FTX_PROTOCOL_FT4:
        ft4_encode(msg.payload, tones);
        symbol_bt = FT4_SYMBOL_BT;
        break;
    }

    int16_t *out = gfsk_synth(tones, n_tones, signal_freq, symbol_bt, symbol_period, sample_rate, n_samples);
    if (!out) {
        LV_LOG_ERROR("gfsk_synth allocation failed");
        return false;
    }
    *samples = out;
    return true;
}

void ftx_worker_put_rx_samples(float complex *samples, uint32_t n_samples) {
    if (wf.num_blocks >= wf.max_blocks) {
        LV_LOG_ERROR("FT8 wf is full");
        return;
    }

    if (n_samples != block_size) {
        LV_LOG_ERROR("n_samples(%llu) is not equal expected block size(%llu)", n_samples, block_size);
        return;
    }

    complex float *frame_ptr;
    int            offset = wf.num_blocks * wf.block_stride;
    int            frame_pos = 0;

    for (int time_sub = 0; time_sub < wf.time_osr; time_sub++) {
        windowcf_write(frame_window, &samples[frame_pos], subblock_size);
        frame_pos += subblock_size;

        windowcf_read(frame_window, &frame_ptr);

        liquid_vectorcf_mul(rx_window, frame_ptr, nfft, time_buf);

        fft_execute(fft);

        for (int freq_sub = 0; freq_sub < wf.freq_osr; freq_sub++)
            for (int bin = 0; bin < wf.num_bins; bin++) {
                int           src_bin = (bin * wf.freq_osr) + freq_sub;
                complex float freq = freq_buf[src_bin];
                float         mag2 = crealf(freq * conjf(freq));
                float         db = 10.0f * log10f(mag2);
                int           scaled = (int16_t)(db * 2.0f + 240.0f);

                if (scaled < 0) {
                    scaled = 0;
                } else if (scaled > 255) {
                    scaled = 255;
                }

                wf.mag[offset] = scaled;
                offset++;
            }
    }
    wf.num_blocks++;
}

void ftx_worker_decode(decoded_msg_cb msg_cb, bool last, void *user_data) {
    if (wf.num_blocks >= find_candidates_at) {
        if (num_candidates == 0) {
            num_candidates = ftx_find_candidates(&wf, MAX_CANDIDATES, candidate_list, ft8lib_min_score);
        } else if (last) {
            // Last decoding
            decode_messages(&wf, &num_candidates, candidate_list, decoded, decoded_hashtable, LDPC_ITERATIONS, msg_cb,
                            user_data);
        } else if (wf.num_blocks % DECODE_BLOCK_STRIDE == 0) {
            // incremental decoding
            decode_messages(&wf, &num_candidates, candidate_list, decoded, decoded_hashtable, EARLY_LDPC_ITERATIONS,
                            msg_cb, user_data);
        }
    }
}

int ftx_worker_get_block_size() {
    return block_size;
}

bool ftx_worker_is_full() {
    return wf.max_blocks <= wf.num_blocks;
}

static void decode_messages(const ftx_waterfall_t *wf, int *num_candidates, ftx_candidate_t *candidate_list,
                            ftx_message_t *decoded, ftx_message_t **decoded_hashtable, int ldpc_iterations,
                            decoded_msg_cb msg_cb, void *user_data) {
    // Go over candidates and attempt to decode messages

    int to_delete_idx[MAX_CANDIDATES];
    int to_delete_size = 0;

    for (int idx = 0; idx < *num_candidates; ++idx) {
        const ftx_candidate_t *cand = &candidate_list[idx];

        // Skip candidates, that are not fully received
        if ((cand->time_offset + n_tones - sync_num) >= wf->num_blocks) {
            continue;
        }
        
        // Safety check: prevent buffer overflow
        if (to_delete_size >= MAX_CANDIDATES) {
            LV_LOG_WARN("Too many candidates to delete, truncating to %d", MAX_CANDIDATES);
            break;
        }
        
        to_delete_idx[to_delete_size++] = idx;

        ftx_message_t       message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(wf, cand, ldpc_iterations, &message, &status)) {
            if (status.ldpc_errors > 0) {
                LV_LOG_INFO("LDPC decode: %d errors", status.ldpc_errors);
            } else if (status.crc_calculated != status.crc_extracted) {
                LV_LOG_INFO("CRC mismatch!");
            }
            continue;
        }

        float freq_hz = (cand->freq_offset + (float)cand->freq_sub / FREQ_OSR) / symbol_period;
        float time_sec = (cand->time_offset + (float)cand->time_sub / TIME_OSR) * symbol_period;

        LV_LOG_INFO("Checking hash table for %4.1fs / %4.1fHz [%d]...", time_sec, freq_hz, cand->score);
        int  idx_hash = message.hash % MAX_DECODED_MESSAGES;
        bool found_empty_slot = false;
        bool found_duplicate = false;
        int  probes = 0;
        do {
            if (decoded_hashtable[idx_hash] == NULL) {
                LV_LOG_INFO("Found an empty slot");
                found_empty_slot = true;
            } else if ((decoded_hashtable[idx_hash]->hash == message.hash) &&
                       (0 == memcmp(decoded_hashtable[idx_hash]->payload, message.payload, sizeof(message.payload)))) {
                LV_LOG_INFO("Found a duplicate!");
                found_duplicate = true;
            } else {
                LV_LOG_INFO("Hash table clash!");
                idx_hash = (idx_hash + 1) % MAX_DECODED_MESSAGES;
                probes++;
            }
        } while (!found_empty_slot && !found_duplicate && probes < MAX_DECODED_MESSAGES);

        if (found_empty_slot) {
            // Fill the empty hashtable slot
            memcpy(&decoded[idx_hash], &message, sizeof(message));
            decoded_hashtable[idx_hash] = &decoded[idx_hash];

            char             text[FTX_MAX_MESSAGE_LENGTH];
            ftx_message_rc_t unpack_status = ftx_message_decode(&message, &hash_if, text);
            if (unpack_status != FTX_MESSAGE_RC_OK) {
                LV_LOG_INFO("Error [%d] while unpacking!", (int)unpack_status);
            } else {
                int snr = get_message_snr(wf, cand, &message);
                msg_cb(text, snr, freq_hz, time_sec, user_data);
            }
        }
    }
    // Remove decoded candidate;
    ftx_delete_candidates(to_delete_idx, to_delete_size, candidate_list, num_candidates);
}

static int get_message_snr(const ftx_waterfall_t *wf, const ftx_candidate_t *candidate, ftx_message_t *msg) {
    uint8_t n_tones = (wf->protocol == FTX_PROTOCOL_FT4) ? FT4_NN : FT8_NN;
    uint8_t tones[n_tones];

    if (wf->protocol == FTX_PROTOCOL_FT4) {
        ft4_encode(msg->payload, tones);
    } else {
        ft8_encode(msg->payload, tones);
    }
    return ftx_get_snr(wf, candidate, tones, n_tones);
}
