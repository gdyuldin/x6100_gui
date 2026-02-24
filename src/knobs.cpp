/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 *  Copyright (c) 2025 Georgy Dyuldin R2RFE
 */


#include "knobs.h"

#include "buttons.h"

#include <string>
#include <vector>
#include <map>

extern "C" {
    #include "styles.h"

    #include <stdio.h>
    #include <stdlib.h>
}

#define KNOBS_HEIGHT 26
#define KNOBS_STATIC_WIDTH 24
#define KNOBS_PADDING 2
#define KNOBS_DYNAMIC_WIDTH 400

#define COLOR_ACTIVE "70ff70"
#define COLOR_INACTIVE "b0b0b0"
#define MFK_FMT

enum modes_t {
    MODE_EDIT,
    MODE_SELECT,
};

/* Knob items classes - for each of possible knob action */

struct Control {
    const char *name;

    Control(const char *name) : name(name) {};

    virtual std::string to_str()=0;

    virtual ObserverDelayed *subscribe(void (*cb)(Subject *, void *), void *user_data) {
        return nullptr;
    }

  protected:
    static std::string float_to_str(float val, std::string fmt) {
        size_t len = snprintf(nullptr, 0, fmt.c_str(), val);
        char  *buf = (char *)malloc(len + 1);
        sprintf(buf, fmt.c_str(), val);
        return std::string(buf);
    }
};

struct ControlSubj: public Control {
    Subject **subj;

    ControlSubj(const char *name, Subject **subj): Control(name), subj(subj) {};

    ObserverDelayed *subscribe(void (*cb)(Subject *, void *), void *user_data) {
        return (*subj)->subscribe_delayed(cb, user_data);
    }
};

struct ControlSubjInt: public ControlSubj {
    using ControlSubj::ControlSubj;

    std::string to_str() {
        return std::to_string(subject_get_int(*subj));
    }

};

struct ControlSubjFloat: public ControlSubj {
    std::string fmt;

    ControlSubjFloat(const char *name, Subject **subj, std::string fmt="%0.1f"): ControlSubj(name, subj), fmt(fmt) {};

    std::string to_str() {
        float val = subject_get_float(*subj);
        return float_to_str(val, fmt);
    }
};

struct ControlSubjChoices: public ControlSubj {
    std::vector<std::string> choices;

    ControlSubjChoices(const char *name, Subject **subj, std::vector<std::string> choices): ControlSubj(name, subj), choices(choices) {};

    std::string to_str() {
        int32_t val = subject_get_int(*subj);
        if ((choices.size() > val) && (val >= 0)) {
            return choices[val];
        } else {
            return std::string("Unknown");
        }
    }
};

struct ControlSubjOnOff: public ControlSubjChoices {
    ControlSubjOnOff(const char *name, Subject **subj): ControlSubjChoices(name, subj, {"Off", "On"}) {};
};


template <typename T> struct ControlInt: public Control {
    T *val;

    ControlInt(const char *name, T *val): Control(name), val(val) {};

    std::string to_str() {
        return std::to_string(*val);
    }
};

template <typename T> struct ControlChoices: public Control {
    T *val;
    std::vector<std::string> choices;

    ControlChoices(const char *name, T *val, std::vector<std::string> choices): Control(name), val(val), choices(choices) {};

    std::string to_str() {
        if ((choices.size() > *val) && (*val >= 0)) {
            return choices[*val];
        } else {
            return std::string("Unknown");
        }
    }
};

struct ControlComp: public ControlSubj {
    using ControlSubj::ControlSubj;

    std::string to_str() {
        return std::string(params_comp_str_get(subject_get_int(*subj)));
    }
};


/* Knob info - class for displaying information about knobs */

class KnobInfo {
    lv_obj_t         **label=nullptr;
    Control         *item=nullptr;
    const std::string arrow_symbol;
    modes_t           mode = MODE_EDIT;

    ObserverDelayed *observer=nullptr;

    void update() {
        if (!item) {
            return;
        }
        if (!*label) {
            return;
        }
        std::string val = item->to_str();
        char buf[64];
        snprintf(buf, 64, "%s #%s %s:# #%s %s#", arrow_symbol.c_str(),
                 mode == MODE_EDIT ? COLOR_INACTIVE : COLOR_ACTIVE, item->name,
                 mode == MODE_SELECT ? COLOR_INACTIVE : COLOR_ACTIVE, val.c_str());
        lv_label_set_text(*label, buf);
    }

    static void on_subj_change(Subject *subj, void *user_data) {
        KnobInfo *obj = (KnobInfo *)user_data;
        obj->update();
    }

  public:
    KnobInfo(lv_obj_t **label, const std::string arrow_symbol) : label(label), arrow_symbol(arrow_symbol) {};

    void set_edit_mode(bool edit) {
        if (edit) {
            mode = MODE_EDIT;
        } else {
            mode = MODE_SELECT;
        }
        update();
    }

    void set_ctrl(Control *item) {
        if (item == this->item) {
            update();
        } else {
            if (observer) {
                delete observer;
                observer = nullptr;
            }
            this->item = item;
            observer = item->subscribe(on_subj_change, (void *)this);
            if (observer) {
                observer->notify();
            } else {
                update();
            }
        }
    }
};

static void on_knob_info_enabled_change(Subject *subj, void *user_data);


static std::map<int, Control*> controls = {
    {CTRL_VOL, new ControlSubjInt("Volume", &cfg.vol.val)},
    {CTRL_VOL, new ControlSubjInt("Volume", &cfg.vol.val)},
    {CTRL_SQL, new ControlSubjInt("Voice SQL", &cfg.sql.val)},
    {CTRL_RFG, new ControlSubjInt("RF gain", &cfg_cur.band->rfg.val)},
    {CTRL_FILTER_LOW, new ControlSubjInt("Filter low", &cfg_cur.filter.low)},
    {CTRL_FILTER_HIGH, new ControlSubjInt("Filter high", &cfg_cur.filter.high)},
    {CTRL_FILTER_BW, new ControlSubjInt("Filter bw", &cfg_cur.filter.bw)},
    {CTRL_PWR, new ControlSubjFloat("Power", &cfg.pwr.val, "%0.1f")},
    {CTRL_MIC, new ControlSubjChoices("MIC", &cfg.mic.val, {"Built-In", "Handle", "Auto"})},
    {CTRL_HMIC, new ControlSubjInt("H-MIC gain", &cfg.hmic.val)},
    {CTRL_IMIC, new ControlSubjInt("I-MIC gain", &cfg.imic.val)},
    {CTRL_MONI, new ControlSubjInt("Moni level", &cfg.moni.val)},
    {CTRL_SPECTRUM_FACTOR, new ControlSubjInt("Zoom", &cfg_cur.zoom)},
    {CTRL_COMP, new ControlComp("Compressor", &cfg.comp.val)},
    {CTRL_ANT, new ControlSubjInt("Ant", &cfg.ant_id.val)},
    {CTRL_RIT, new ControlSubjInt("RIT", &cfg.rit.val)},
    {CTRL_XIT, new ControlSubjInt("XIT", &cfg.xit.val)},
    {CTRL_IF_SHIFT, new ControlSubjInt("IF shift", &cfg_cur.band->if_shift.val)},

    {CTRL_DNF, new ControlSubjOnOff("Notch filter", &cfg.dnf.val)},
    {CTRL_DNF_CENTER, new ControlSubjInt("DNF center", &cfg.dnf_center.val)},
    {CTRL_DNF_WIDTH, new ControlSubjInt("DNF width", &cfg.dnf_width.val)},
    {CTRL_DNF_AUTO, new ControlSubjOnOff("DNF auto", &cfg.dnf_auto.val)},
    {CTRL_NB, new ControlSubjOnOff("Noise blanker", &cfg.nb.val)},
    {CTRL_NB_LEVEL, new ControlSubjInt("NB level", &cfg.nb_level.val)},
    {CTRL_NB_WIDTH, new ControlSubjInt("NB width", &cfg.nb_width.val)},
    {CTRL_NR, new ControlSubjOnOff("Noise reduction", &cfg.nr.val)},
    {CTRL_NR_LEVEL, new ControlSubjInt("NR level", &cfg.nr_level.val)},

    {CTRL_AGC_HANG, new ControlSubjOnOff("AGC hang", &cfg.agc_hang.val)},
    {CTRL_AGC_KNEE, new ControlSubjInt("AGC knee", &cfg.agc_knee.val)},
    {CTRL_AGC_SLOPE, new ControlSubjInt("AGC slope", &cfg.agc_slope.val)},

    {CTRL_KEY_SPEED, new ControlSubjInt("Key speed", &cfg.key_speed.val)},
    {CTRL_KEY_TRAIN, new ControlSubjOnOff("Key train", &cfg.key_train.val)},
    {CTRL_KEY_MODE, new ControlSubjChoices("Key mode", &cfg.key_mode.val, {"Manual", "Auto-L", "Auto-R"})},
    {CTRL_IAMBIC_MODE, new ControlSubjChoices("Iambic mode", &cfg.iambic_mode.val, {"A", "B"})},
    {CTRL_KEY_TONE, new ControlSubjInt("Key tone", &cfg.key_tone.val)},
    {CTRL_KEY_VOL, new ControlSubjInt("Key vol", &cfg.key_vol.val)},
    {CTRL_QSK_TIME, new ControlSubjInt("QSK time", &cfg.qsk_time.val)},
    {CTRL_KEY_RATIO, new ControlSubjFloat("Key ratio", &cfg.key_ratio.val)},
    {CTRL_CW_DECODER, new ControlSubjOnOff("CW decoder", &cfg.cw_decoder.val)},
    {CTRL_CW_TUNE, new ControlSubjOnOff("CW tuner", &cfg.cw_tune.val)},
    {CTRL_CW_DECODER_SNR, new ControlSubjFloat("CW decoded snr", &cfg.cw_decoder_snr.val)},
    {CTRL_CW_DECODER_PEAK_BETA, new ControlSubjFloat("CW decoder peak beta", &cfg.cw_decoder_peak_beta.val, "%0.2f")},
    {CTRL_CW_DECODER_NOISE_BETA,
     new ControlSubjFloat("CW decoder noise beta", &cfg.cw_decoder_noise_beta.val, "%0.2f")},
    // {MFK_RTTY_RATE, Control("RTTY rate", []() { return to_str((float)params.rtty_rate / 100.0f, "%0.2f"); })},
    // {MFK_RTTY_SHIFT, Control("RTTY shift", []() { return std::to_string(params.rtty_shift); })},
    // {MFK_RTTY_CENTER, Control("RTTY center", []() { return std::to_string(params.rtty_center); })},
    // {MFK_RTTY_REVERSE, Control("RTTY reverse", []() { return std::string(params.rtty_reverse ? "On" : "Off"); })},
};

static lv_obj_t *vol_info;

static lv_obj_t *mfk_info;

static KnobInfo *vol_knob_info = new KnobInfo(&vol_info, LV_SYMBOL_UP);
static KnobInfo *mfk_knob_info = new KnobInfo(&mfk_info, LV_SYMBOL_DOWN);

static bool enabled;


void knobs_init(lv_obj_t * parent) {
    // Basic positon calculation
    uint16_t y = 480 - BTN_HEIGHT - 5;
    uint16_t x_static = KNOBS_PADDING;
    uint16_t x_dynamic = x_static  + KNOBS_STATIC_WIDTH + KNOBS_PADDING;

    // Init
    vol_info = lv_label_create(parent);
    lv_obj_add_style(vol_info, &knobs_style, 0);
    lv_obj_set_pos(vol_info, x_static, y - KNOBS_HEIGHT * 2);
    lv_label_set_recolor(vol_info, true);
    lv_label_set_text(vol_info, "");
    // vol_knob_info = new KnobInfo(vol_info, LV_SYMBOL_UP);
    vol_knob_info->set_edit_mode(true);

    mfk_info = lv_label_create(parent);
    lv_obj_add_style(mfk_info, &knobs_style, 0);
    lv_obj_set_pos(mfk_info, x_static, y - KNOBS_HEIGHT * 1);
    lv_label_set_recolor(mfk_info, true);
    lv_label_set_text(mfk_info, "");
    // mfk_knob_info = new KnobInfo(mfk_info, LV_SYMBOL_DOWN);
    mfk_knob_info->set_edit_mode(true);

    subject_add_delayed_observer_and_call(cfg.knob_info.val, on_knob_info_enabled_change, nullptr);
}

void knobs_display(bool on) {
    if (on && enabled) {
        lv_obj_clear_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    }
}

bool knobs_visible() {
    return vol_info && !lv_obj_has_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
}

/* VOL */

void knobs_set_vol_state(bool edit) {
    vol_knob_info->set_edit_mode(edit);
}

void knobs_set_vol_param(cfg_ctrl_t control) {
    Control *item;
    try {
        item = controls.at(control);
    } catch (const std::out_of_range &ex) {
        LV_LOG_WARN("VOL Control %d is unknown, skip, %s", control, ex.what());
        return;
    }
    vol_knob_info->set_ctrl(item);
}

/* MFK */

void knobs_set_mfk_state(bool edit) {
    mfk_knob_info->set_edit_mode(edit);
}

void knobs_set_mfk_param(cfg_ctrl_t control) {
    Control *item;
    try {
        item = controls.at(control);
    } catch (const std::out_of_range &ex) {
        LV_LOG_WARN("MFK Control %d is unknown, skip, %s", control, ex.what());
        return;
    }
    mfk_knob_info->set_ctrl(item);
}


static void on_knob_info_enabled_change(Subject *subj, void *user_data) {
    enabled = subject_get_int(subj);
}
