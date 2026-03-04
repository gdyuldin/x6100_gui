#include "cfg.private.h"

#include "atu.private.h"
#include "band.private.h"
#include "mode.private.h"
#include "params.private.h"
#include "transverter.private.h"
#include "memory.private.h"
#include "digital_modes.private.h"

#include "../lvgl/lvgl.h"
#include "../util.h"
#include <aether_radio/x6100_control/control.h>
#include <ft8lib/constants.h>

#include <stdio.h>
#include <stdlib.h>

cfg_t cfg;

cfg_cur_t cfg_cur = {
    .band = &cfg_band,
};

static band_info_t cur_band_info;

static int init_params_cfg(sqlite3 *db);
// static int init_band_cfg(sqlite3 *db);
// static int init_mode_cfg(sqlite3 *db);


static void *params_save_thread(void *arg);

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_item_change(Subject *subj, void *user_data);
static void on_vfo_change(Subject *subj, void *user_data);
// static void on_band_id_change(Subject *subj, void *user_data);
static void on_ab_freq_change(Subject *subj, void *user_data);
static void on_ab_mode_change(Subject *subj, void *user_data);
static void update_cur_low_filter(Subject *subj, void *user_data);
static void update_cur_high_filter(Subject *subj, void *user_data);
static void on_freq_step_change(Subject *subj, void *user_data);
static void on_zoom_change(Subject *subj, void *user_data);

static void on_bg_freq_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);
static void on_cur_filter_low_change(Subject *subj, void *user_data);
static void on_cur_filter_high_change(Subject *subj, void *user_data);
static void on_cur_filter_bw_change(Subject *subj, void *user_data);
static void on_cur_freq_step_change(Subject *subj, void *user_data);
static void on_cur_zoom_change(Subject *subj, void *user_data);


// All allowed modes for VOL fast access
cfg_ctrl_t cfg_encoder_vol_modes_default[] = {
    CTRL_VOL,
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
};

// All allowed modes for MFK fast access
cfg_ctrl_t cfg_encoder_mfk_modes_default[] = {
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
};


// #define TEST_CFG
#ifdef TEST_CFG
#include "test_cfg.c"
#endif


int cfg_init(sqlite3 *db) {
    int rc;

    rc = init_params_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading params");
        // return rc;
    }
    // rc = init_band_cfg(db);
    // if (rc != 0) {
    //     LV_LOG_ERROR("Error during loading band params");
    //     return rc;
    // }
    cfg_band_params_init(db);
    // cfg_cur.band = &cfg_band;

    cfg_mode_params_init(db);
    // rc = init_mode_cfg(db);
    // if (rc != 0) {
    //     LV_LOG_ERROR("Error during loading mode params");
    //     return rc;
    // }

    cfg_atu_init(db);
    cfg_cur.atu = &atu_network;

    cfg_transverter_init(db);
    cfg_memory_init(db);
    cfg_digital_modes_init(db);

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);

#ifdef TEST_CFG
    run_tests();
#endif
    return rc;
}

/**
 * Delayed save of item
 */
static void on_item_change(Subject *subj, void *user_data) {
    cfg_item_t *item = (cfg_item_t *)user_data;
    pthread_mutex_lock(&item->dirty->mux);
    if (item->dirty->val != ITEM_STATE_LOADING) {
        item->dirty->val = ITEM_STATE_CHANGED;
        LV_LOG_INFO("Set dirty %s (pk=%i)", item->db_name, item->pk);
    }
    pthread_mutex_unlock(&item->dirty->mux);
}

/**
 * Changing of key tone
 */
static void on_key_tone_change(Subject *subj, void *user_data) {
    int32_t key_tone = subject_get_int(subj);
    if (cfg_cur.mode == NULL) {
        LV_LOG_USER("Skip update filters, cfg_cur.mode is not initialized");
        return;
    }
    x6100_mode_t db_mode = xmode_2_db(subject_get_int(cfg_cur.mode));
    if (db_mode == x6100_mode_cw) {
        int32_t high, low, bw;
        bw   = subject_get_int(cfg_cur.filter.bw);
        low  = key_tone - bw / 2;
        high = low + bw;
        subject_set_int(cfg_cur.filter.high, high);
        subject_set_int(cfg_cur.filter.low, low);
    }
}

/**
 * Init cfg items
 */
void init_items(cfg_item_t *cfg_arr, uint32_t count, int (*load)(struct cfg_item_t *item),
                int (*save)(struct cfg_item_t *item)) {
    for (size_t i = 0; i < count; i++) {
        cfg_item_t *item = &cfg_arr[i];
        if (load) {
            if (item->load) {
                LV_LOG_ERROR("Item %s load func is redefined", item->db_name);
            }
            item->load = load;
        } else if (!item->load) {
            LV_LOG_ERROR("Item %s has no defined load func", item->db_name);
        }
        if (save) {
            if (item->save) {
                LV_LOG_ERROR("Item %s save func is redefined", item->db_name);
            }
            item->save = save;
        } else if (!item->save) {
            LV_LOG_ERROR("Item %s has no defined save func", item->db_name);
        }
        item->dirty      = malloc(sizeof(*item->dirty));
        item->dirty->val = ITEM_STATE_CLEAN;
        pthread_mutex_init(&item->dirty->mux, NULL);
        Observer *o = subject_add_observer(item->val, on_item_change, item);
    }
}
/**
 * Load items from db
 */
int load_items_from_db(cfg_item_t *cfg_arr, uint32_t count) {
    int rc;
    for (size_t i = 0; i < count; i++) {
        cfg_arr[i].dirty->val = ITEM_STATE_LOADING;
        rc = cfg_arr[i].load(&cfg_arr[i]);
        if (rc == NOT_FOUND) {
            LV_LOG_USER("%s is not found (pk=%i), rc=%d", cfg_arr[i].db_name, cfg_arr[i].pk, rc);
            cfg_arr[i].save(&cfg_arr[i]);
        } else if (rc != SUCCESS) {
            LV_LOG_WARN("Can't load %s (pk=%i), rc=%d", cfg_arr[i].db_name, cfg_arr[i].pk, rc);
        }
        cfg_arr[i].dirty->val = ITEM_STATE_CLEAN;
    }
    return rc;
}

/**
 * Save items to db
 */

void save_item_to_db(cfg_item_t *item, bool force) {
    int rc;
    pthread_mutex_lock(&item->dirty->mux);
    if ((item->dirty->val == ITEM_STATE_CHANGED) || force) {
        rc = item->save(item);
        if (rc != 0) {
            LV_LOG_USER("Can't save %s (pk=%i)", item->db_name, item->pk);
        }
        item->dirty->val = ITEM_STATE_CLEAN;
    }
    pthread_mutex_unlock(&item->dirty->mux);
}

void save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size) {
    int rc;
    for (size_t i = 0; i < cfg_size; i++) {
        save_item_to_db(&cfg_arr[i], false);
    }
}


/**
 * Helpers for initialization
 */
void fill_cfg_item_int(cfg_item_t *item, Subject * val, const char * db_name) {
    item->db_name = strdup(db_name);
    item->val = val;
    item->load = cfg_params_load_item_int;
    item->save = cfg_params_save_item_int;
}

void fill_cfg_item_uint64(cfg_item_t *item, Subject * val, const char * db_name) {
    item->db_name = strdup(db_name);
    item->val = val;
    item->load = cfg_params_load_item_uint64;
    item->save = cfg_params_save_item_uint64;
}

void fill_cfg_item_float(cfg_item_t *item, Subject * val, float db_scale, const char * db_name) {
    item->db_name = strdup(db_name);
    item->db_scale = db_scale;
    item->val = val;
    item->load = cfg_params_load_item_float;
    item->save = cfg_params_save_item_float;
}

void fill_cfg_item_enc_str(cfg_item_t *item, Subject * val, const char * db_name) {
    item->db_name = strdup(db_name);
    item->val = val;
    item->load = cfg_params_load_item_str;
    item->save = cfg_params_save_item_str;
}

/**
 * Helpers for db <-> val conversion
 */

static void * db_to_val_encoder_bind(void * val, Subject *subj) {
    size_t target_len = strlen(subject_get_text(subj));
    char *str = strdup((char *)val);
    size_t new_len = strlen(str);
    if (new_len > target_len) {
        // Truncate
        str[target_len] = '\0';
    } else if (new_len < target_len) {
        str = realloc(str, target_len + 1);
        for (size_t i = new_len; i < target_len; i++) {
            str[i] = ENCODER_BIND_NONE;
        }
        str[target_len] = '\0';
    }
    return (void *)str;
}

/**
 * Save thread
 */
static void *params_save_thread(void *arg) {
    cfg_item_t *cfg_arr;
    cfg_arr           = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg) / sizeof(cfg_item_t);

    cfg_item_t *cfg_band_arr;
    cfg_band_arr           = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t *cfg_mode_arr;
    cfg_mode_arr           = (cfg_item_t *)&cfg_mode;
    uint32_t cfg_mode_size = sizeof(cfg_mode) / sizeof(cfg_item_t);

    cfg_item_t *cfg_transverter_arr;
    cfg_transverter_arr           = (cfg_item_t *)&cfg_transverters;
    uint32_t cfg_transverter_size = sizeof(cfg_transverters) / sizeof(cfg_item_t);

    while (true) {
        save_items_to_db(cfg_arr, cfg_size);
        save_items_to_db(cfg_band_arr, cfg_band_size);
        save_items_to_db(cfg_mode_arr, cfg_mode_size);
        save_items_to_db(cfg_transverter_arr, cfg_transverter_size);
        sleep_usec(10000000);
    }
}

/**
 * Initialization functions
 */
static int init_params_cfg(sqlite3 *db) {
    /* Init db modules */
    cfg_params_init(db);

    /* Fill configuration */
    char encoder_bind[CTRL_FAST_ACCESS_LAST + 1];
    memset(encoder_bind, ENCODER_BIND_NONE, CTRL_FAST_ACCESS_LAST);
    encoder_bind[CTRL_FAST_ACCESS_LAST] = '\0';

    // Set default values
    encoder_bind[CTRL_VOL] = ENCODER_BIND_VOL;
    encoder_bind[CTRL_RFG] = ENCODER_BIND_VOL;
    encoder_bind[CTRL_FILTER_LOW] = ENCODER_BIND_VOL;
    encoder_bind[CTRL_FILTER_HIGH] = ENCODER_BIND_VOL;
    encoder_bind[CTRL_PWR] = ENCODER_BIND_VOL;
    encoder_bind[CTRL_HMIC] = ENCODER_BIND_VOL;

    encoder_bind[CTRL_SPECTRUM_FACTOR] = ENCODER_BIND_MFK;
    encoder_bind[CTRL_AGC_KNEE] = ENCODER_BIND_MFK;
    encoder_bind[CTRL_DNF] = ENCODER_BIND_MFK;

    fill_cfg_item_enc_str(&cfg.encoders_binds, subject_create_text(encoder_bind), "encoder_bind");
    // Process db values. Heplful for future changes
    cfg.encoders_binds.db_to_val = db_to_val_encoder_bind;

    fill_cfg_item_int(&cfg.vol, subject_create_int(20), "vol");
    fill_cfg_item_int(&cfg.sql, subject_create_int(0), "sql");
    fill_cfg_item_float(&cfg.pwr, subject_create_float(5.0f), 0.1f, "pwr");
    fill_cfg_item_float(&cfg.output_gain, subject_create_float(0.0f), 0.2f, "output_gain");

    fill_cfg_item_int(&cfg.mic, subject_create_int(x6100_mic_auto), "mic");
    fill_cfg_item_int(&cfg.hmic, subject_create_int(20), "hmic");
    fill_cfg_item_int(&cfg.imic, subject_create_int(30), "imic");
    fill_cfg_item_int(&cfg.moni, subject_create_int(30), "moni");

    fill_cfg_item_int(&cfg.key_tone, subject_create_int(700), "key_tone");
    fill_cfg_item_int(&cfg.band_id, subject_create_int(5), "band");
    fill_cfg_item_int(&cfg.ant_id, subject_create_int(1), "ant");
    fill_cfg_item_int(&cfg.atu_enabled, subject_create_int(false), "atu");
    fill_cfg_item_int(&cfg.comp, subject_create_int(4), "comp");
    fill_cfg_item_float(&cfg.comp_threshold_offset, subject_create_float(0.0f), 0.5f, "comp_threshold_offset");
    fill_cfg_item_float(&cfg.comp_makeup_offset, subject_create_float(0.0f), 0.5f, "comp_makeup_offset");

    fill_cfg_item_int(&cfg.rit, subject_create_int(0), "rit");
    fill_cfg_item_int(&cfg.xit, subject_create_int(0), "xit");
    fill_cfg_item_int(&cfg.fm_emphasis, subject_create_int(0), "fm_emphasis");

    /* UI */
    fill_cfg_item_int(&cfg.auto_level_enabled, subject_create_int(true), "auto_level_enabled");
    fill_cfg_item_float(&cfg.auto_level_offset, subject_create_float(0.0f), 0.5f, "auto_level_offset");
    fill_cfg_item_int(&cfg.knob_info, subject_create_int(true), "knob_info");

    /* Key */

    fill_cfg_item_int(&cfg.key_speed, subject_create_int(15), "key_speed");
    fill_cfg_item_int(&cfg.key_mode, subject_create_int(x6100_key_manual), "key_mode");
    fill_cfg_item_int(&cfg.iambic_mode, subject_create_int(x6100_iambic_a), "iambic_mode");
    fill_cfg_item_int(&cfg.key_vol, subject_create_int(10), "key_vol");
    fill_cfg_item_int(&cfg.key_train, subject_create_int(false), "key_train");
    fill_cfg_item_int(&cfg.qsk_time, subject_create_int(100), "qsk_time");
    fill_cfg_item_float(&cfg.key_ratio, subject_create_float(3.0f), 0.1f, "key_ratio");

    /* CW decoder */
    fill_cfg_item_int(&cfg.cw_decoder, subject_create_int(true), "cw_decoder");
    fill_cfg_item_int(&cfg.cw_tune, subject_create_int(false), "cw_tune");
    fill_cfg_item_float(&cfg.cw_decoder_snr, subject_create_float(5.0f), 0.1f, "cw_decoder_snr_2");
    fill_cfg_item_float(&cfg.cw_decoder_snr_gist, subject_create_float(1.0f), 0.1f, "cw_decoder_snr_gist");
    fill_cfg_item_float(&cfg.cw_decoder_peak_beta, subject_create_float(0.10f), 0.01f, "cw_decoder_peak_beta");
    fill_cfg_item_float(&cfg.cw_decoder_noise_beta, subject_create_float(0.80f), 0.01f, "cw_decoder_noise_beta");

    fill_cfg_item_int(&cfg.agc_hang, subject_create_int(false), "agc_hang");
    fill_cfg_item_int(&cfg.agc_knee, subject_create_int(-60), "agc_knee");
    fill_cfg_item_int(&cfg.agc_slope, subject_create_int(6), "agc_slope");

    // DSP
    fill_cfg_item_int(&cfg.dnf, subject_create_int(false), "dnf");
    fill_cfg_item_int(&cfg.dnf_center, subject_create_int(1000), "dnf_center");
    fill_cfg_item_int(&cfg.dnf_width, subject_create_int(50), "dnf_width");
    fill_cfg_item_int(&cfg.dnf_auto, subject_create_int(false), "dnf_auto");

    fill_cfg_item_int(&cfg.nb, subject_create_int(false), "nb");
    fill_cfg_item_int(&cfg.nb_level, subject_create_int(10), "nb_level");
    fill_cfg_item_int(&cfg.nb_width, subject_create_int(10), "nb_width");

    fill_cfg_item_int(&cfg.nr, subject_create_int(false), "nr");
    fill_cfg_item_int(&cfg.nr_level, subject_create_int(0), "nr_level");

    // SWR scan
    fill_cfg_item_int(&cfg.swrscan_linear, subject_create_int(true), "swrscan_linear");
    fill_cfg_item_int(&cfg.swrscan_span, subject_create_int(200000), "swrscan_span");

    // FT8
    fill_cfg_item_int(&cfg.ft8_show_all, subject_create_int(true), "ft8_show_all");
    fill_cfg_item_int(&cfg.ft8_protocol, subject_create_int(FTX_PROTOCOL_FT8), "ft8_protocol");
    fill_cfg_item_int(&cfg.ft8_auto, subject_create_int(true), "ft8_auto");
    fill_cfg_item_int(&cfg.ft8_hold_freq, subject_create_int(true), "ft8_hold_freq");
    fill_cfg_item_int(&cfg.ft8_max_repeats, subject_create_int(6), "ft8_max_repeats");
    fill_cfg_item_int(&cfg.ft8_omit_cq_qth, subject_create_int(false), "ft8_omit_cq_qth");

    /* Bind callbacks */
    // subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);
    subject_add_observer(cfg.key_tone.val, on_key_tone_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr  = (cfg_item_t *)&cfg;
    uint32_t    cfg_size = sizeof(cfg) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size, NULL, NULL);
    return load_items_from_db(cfg_arr, cfg_size);
}
