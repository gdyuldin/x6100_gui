#include "lock_manager.h"

#include "pubsub_ids.h"

static bool lock_ab = false;
static bool lock_mode = false;
static bool lock_freq = false;
static bool lock_band = false;

void lm_set_ab(bool val) {
    if (val != lock_ab) {
        lock_ab = val;
        lv_msg_send(MSG_LOCK_AB, &lock_ab);
    }
}

void lm_set_mode(bool val) {
    if (val != lock_mode) {
        lock_mode = val;
        lv_msg_send(MSG_LOCK_MODE, &lock_mode);
    }
}

void lm_set_freq(bool val) {
    if (val != lock_freq) {
        lock_freq = val;
        lv_msg_send(MSG_LOCK_FREQ, &lock_freq);
    }
}

void lm_set_band(bool val) {
    if (val != lock_band) {
        lock_band = val;
        lv_msg_send(MSG_LOCK_BAND, &lock_band);
    }
}

void lm_toggle_ab(void) {
    lm_set_ab(!lock_ab);
}

void lm_toggle_mode(void) {
    lm_set_mode(!lock_mode);
}

void lm_toggle_freq(void) {
    lm_set_freq(!lock_freq);
}

void lm_toggle_band(void) {
    lm_set_band(!lock_band);
}

bool lm_get_ab(void) {
    return lock_ab;
}

bool lm_get_mode(void) {
    return lock_mode;
}

bool lm_get_freq(void) {
    return lock_freq;
}

bool lm_get_band(void) {
    return lock_band;
}
