#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "atu.h"
#include "band.h"

#include <pthread.h>
#include <sqlite3.h>

typedef enum {
    ENCODER_BIND_NONE = '_',
    ENCODER_BIND_VOL = 'V',
    ENCODER_BIND_MFK = 'M',
} encoder_binds_t;

typedef enum {
    CTRL_VOL = 0,
    CTRL_SQL,
    CTRL_RFG,
    CTRL_FILTER_LOW,
    CTRL_FILTER_HIGH,
    CTRL_PWR,
    CTRL_HMIC,
    CTRL_MIC,
    CTRL_IMIC,
    CTRL_MONI,
    CTRL_FILTER_BW,

    CTRL_SPECTRUM_FACTOR,

    CTRL_KEY_SPEED,
    CTRL_KEY_MODE,
    CTRL_IAMBIC_MODE,
    CTRL_KEY_TONE,
    CTRL_KEY_VOL,
    CTRL_KEY_TRAIN,
    CTRL_QSK_TIME,
    CTRL_KEY_RATIO,

    CTRL_DNF,
    CTRL_DNF_CENTER,
    CTRL_DNF_WIDTH,
    CTRL_DNF_AUTO,
    CTRL_NB,
    CTRL_NB_LEVEL,
    CTRL_NB_WIDTH,
    CTRL_NR,
    CTRL_NR_LEVEL,

    CTRL_AGC_HANG,
    CTRL_AGC_KNEE,
    CTRL_AGC_SLOPE,
    CTRL_COMP,

    CTRL_CW_DECODER,
    CTRL_CW_TUNE,
    CTRL_CW_DECODER_SNR,
    CTRL_CW_DECODER_PEAK_BETA,
    CTRL_CW_DECODER_NOISE_BETA,

    CTRL_ANT,
    CTRL_RIT,
    CTRL_XIT,
    CTRL_IF_SHIFT,

    CTRL_FAST_ACCESS_LAST,

    /* APPs */

    CTRL_RTTY_RATE,
    CTRL_RTTY_SHIFT,
    CTRL_RTTY_CENTER,
    CTRL_RTTY_REVERSE,

    CTRL_LAST,

} cfg_ctrl_t;

extern cfg_ctrl_t cfg_encoder_vol_modes_default[11];

extern cfg_ctrl_t cfg_encoder_mfk_modes_default[31];


/* configuration structs. Should contain same types (for correct initialization) */
typedef struct {
    cfg_item_t encoders_binds;

    cfg_item_t vol;
    cfg_item_t sql;
    cfg_item_t pwr;
    cfg_item_t output_gain;

    cfg_item_t mic;
    cfg_item_t hmic;
    cfg_item_t imic;
    cfg_item_t moni;

    cfg_item_t key_tone;
    cfg_item_t band_id;
    cfg_item_t ant_id;
    cfg_item_t atu_enabled;
    cfg_item_t comp;
    cfg_item_t comp_threshold_offset;
    cfg_item_t comp_makeup_offset;

    cfg_item_t rit;
    cfg_item_t xit;

    cfg_item_t fm_emphasis;

    cfg_item_t tx_filter_low;
    cfg_item_t tx_filter_high;

    /* UI */
    cfg_item_t auto_level_enabled;
    cfg_item_t auto_level_offset;
    cfg_item_t knob_info;

    /* key */
    cfg_item_t key_speed;
    cfg_item_t key_mode;
    cfg_item_t iambic_mode;
    cfg_item_t key_vol;
    cfg_item_t key_train;
    cfg_item_t qsk_time;
    cfg_item_t key_ratio;

    /* CW decoder */
    cfg_item_t cw_decoder;
    cfg_item_t cw_tune;
    cfg_item_t cw_decoder_snr;
    cfg_item_t cw_decoder_snr_gist;
    cfg_item_t cw_decoder_peak_beta;
    cfg_item_t cw_decoder_noise_beta;

    cfg_item_t agc_hang;
    cfg_item_t agc_knee;
    cfg_item_t agc_slope;

    // DSP
    cfg_item_t dnf;
    cfg_item_t dnf_center;
    cfg_item_t dnf_width;
    cfg_item_t dnf_auto;

    cfg_item_t nb;
    cfg_item_t nb_level;
    cfg_item_t nb_width;

    cfg_item_t nr;
    cfg_item_t nr_level;

    // SWR scan
    cfg_item_t swrscan_linear;
    cfg_item_t swrscan_span;

    // FT8
    cfg_item_t ft8_show_all;
    cfg_item_t ft8_protocol;
    cfg_item_t ft8_auto;
    cfg_item_t ft8_hold_freq;
    cfg_item_t ft8_max_repeats;
    cfg_item_t ft8_omit_cq_qth;
} cfg_t;
extern cfg_t cfg;

/* Current band/mode params */

typedef struct {
    Subject *fg_freq;
    Subject *bg_freq;
    Subject *lo_offset;
    Subject *freq_shift;
    Subject *mode;
    Subject *agc;
    Subject *att;
    Subject *pre;
    struct {
        Subject *low;
        Subject *high;
        Subject *bw;
        struct {
            Subject *from;
            Subject *to;
        } real;
    } filter;
    Subject       *freq_step;
    Subject       *zoom;
    atu_network_t *atu;
    cfg_band_t    *band;
} cfg_cur_t;

extern cfg_cur_t cfg_cur;

int cfg_init(sqlite3 *db);

#ifdef __cplusplus
}
#endif
