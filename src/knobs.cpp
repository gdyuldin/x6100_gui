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
#include "cfg/settings_manager.h"

#include <string>
#include <vector>
#include <stdexcept>
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

    virtual ObserverDelayed* subscribe(observer_cb cb, void *user_data) {
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

template <typename T>
struct ControlSubjBase : public Control {
    SubjectT<T> *subj;

    ControlSubjBase(const char *name, SubjectT<T> *subj)
        : Control(name), subj(subj) {}

    ObserverDelayed* subscribe(observer_cb cb, void *user_data) override {
        return subj->subscribe_delayed(cb, user_data);
    }
};

struct ControlSubjInt : public ControlSubjBase<int32_t> {
    using ControlSubjBase<int32_t>::ControlSubjBase;
    std::string to_str() { return std::to_string(subj->get()); }
};

struct ControlSubjFloat : public ControlSubjBase<float> {
    std::string fmt;
    ControlSubjFloat(const char *name, SubjectT<float> *subj, std::string fmt = "%0.1f")
        : ControlSubjBase<float>(name, subj), fmt(fmt) {}
    std::string to_str() {
        return float_to_str(subj->get(), fmt);
    }
};

struct ControlSubjChoices : public ControlSubjBase<int32_t> {
    std::vector<std::string> choices;
    ControlSubjChoices(const char *name, SubjectT<int32_t> *subj,
                       std::vector<std::string> choices)
        : ControlSubjBase<int32_t>(name, subj), choices(choices) {}
    std::string to_str() {
        int32_t val = subj->get();
        if ((choices.size() > (size_t)val) && (val >= 0)) return choices[val];
        return std::string("Unknown");
    }
};

struct ControlSubjOnOff : public ControlSubjChoices {
    ControlSubjOnOff(const char *name, SubjectT<int32_t> *subj)
        : ControlSubjChoices(name, subj, {"Off", "On"}) {}
};

struct ControlComp : public ControlSubjBase<int32_t> {
    using ControlSubjBase<int32_t>::ControlSubjBase;
    std::string to_str() { return std::string(params_comp_str_get(subj->get())); }
};


/* Knob info - class for displaying information about knobs */

class KnobInfo {
    lv_obj_t         **label=nullptr;
    Control         *item=nullptr;
    const std::string arrow_symbol;
    modes_t           mode = MODE_EDIT;

    Subscription subscription;

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
            this->item = item;
            subscription = Subscription(item->subscribe(on_subj_change, (void *)this));
            subscription->notify();
        }
    }
};

static void on_knob_info_enabled_change(Subject *subj, void *user_data);


static std::map<int, Control*> controls = {
    {CTRL_VOL, new ControlSubjInt("Volume", &cfg_sm.p_volume)},
    {CTRL_VOL, new ControlSubjInt("Volume", &cfg_sm.p_volume)},
    {CTRL_SQL, new ControlSubjInt("Voice SQL", &cfg_sm.p_squelch)},
    {CTRL_RFG, new ControlSubjInt("RF gain", &cfg_sm.p_rfgain)},
    {CTRL_FILTER_LOW, new ControlSubjInt("Filter low", &cfg_sm.cp_cur_filter_low)},
    {CTRL_FILTER_HIGH, new ControlSubjInt("Filter high", &cfg_sm.cp_cur_filter_high)},
    {CTRL_FILTER_BW, new ControlSubjInt("Filter bw", &cfg_sm.cp_cur_filter_bw)},
    {CTRL_PWR, new ControlSubjFloat("Power", &cfg_sm.p_pwr, "%0.1f")},
    {CTRL_MIC, new ControlSubjChoices("MIC", &cfg_sm.p_mic, {"Built-In", "Handle", "Auto"})},
    {CTRL_HMIC, new ControlSubjInt("H-MIC gain", &cfg_sm.p_hmic)},
    {CTRL_IMIC, new ControlSubjInt("I-MIC gain", &cfg_sm.p_imic)},
    {CTRL_MONI, new ControlSubjInt("Moni level", &cfg_sm.p_moni)},
    {CTRL_SPECTRUM_FACTOR, new ControlSubjInt("Zoom", &cfg_sm.p_mode_zoom)},
    {CTRL_COMP, new ControlComp("Compressor", &cfg_sm.p_comp)},

    {CTRL_VOX_ON, new ControlSubjOnOff("VOX", &cfg_sm.p_vox_en)},
    {CTRL_VOX_GAIN, new ControlSubjInt("VOX gain", &cfg_sm.p_vox_gain)},
    {CTRL_VOX_AG, new ControlSubjInt("VOX a-gain", &cfg_sm.p_vox_ag)},
    {CTRL_VOX_DELAY, new ControlSubjInt("VOX delay", &cfg_sm.p_vox_delay)},

    {CTRL_ANT, new ControlSubjInt("Ant", &cfg_sm.p_ant_id)},
    {CTRL_RIT, new ControlSubjInt("RIT", &cfg_sm.p_rit)},
    {CTRL_XIT, new ControlSubjInt("XIT", &cfg_sm.p_xit)},
    {CTRL_IF_SHIFT, new ControlSubjInt("IF shift", &cfg_sm.p_band_if_shift)},

    {CTRL_DNF, new ControlSubjOnOff("Notch filter", &cfg_sm.p_dnf)},
    {CTRL_DNF_CENTER, new ControlSubjInt("DNF center", &cfg_sm.p_dnf_center)},
    {CTRL_DNF_WIDTH, new ControlSubjInt("DNF width", &cfg_sm.p_dnf_width)},
    {CTRL_DNF_AUTO, new ControlSubjOnOff("DNF auto", &cfg_sm.p_dnf_auto)},
    {CTRL_NB, new ControlSubjOnOff("Noise blanker", &cfg_sm.p_nb)},
    {CTRL_NB_LEVEL, new ControlSubjInt("NB level", &cfg_sm.p_nb_level)},
    {CTRL_NB_WIDTH, new ControlSubjInt("NB width", &cfg_sm.p_nb_width)},
    {CTRL_NR, new ControlSubjOnOff("Noise reduction", &cfg_sm.p_nr)},
    {CTRL_NR_LEVEL, new ControlSubjInt("NR level", &cfg_sm.p_nr_level)},

    {CTRL_AGC_HANG, new ControlSubjOnOff("AGC hang", &cfg_sm.p_agc_hang)},
    {CTRL_AGC_KNEE, new ControlSubjInt("AGC knee", &cfg_sm.p_agc_knee)},
    {CTRL_AGC_SLOPE, new ControlSubjInt("AGC slope", &cfg_sm.p_agc_slope)},

    {CTRL_KEY_SPEED, new ControlSubjInt("Key speed", &cfg_sm.p_key_speed)},
    {CTRL_KEY_TRAIN, new ControlSubjOnOff("Key train", &cfg_sm.p_key_train)},
    {CTRL_KEY_MODE, new ControlSubjChoices("Key mode", &cfg_sm.p_key_mode, {"Manual", "Auto-L", "Auto-R"})},
    {CTRL_IAMBIC_MODE, new ControlSubjChoices("Iambic mode", &cfg_sm.p_iambic_mode, {"A", "B"})},
    {CTRL_KEY_TONE, new ControlSubjInt("Key tone", &cfg_sm.p_key_tone)},
    {CTRL_KEY_VOL, new ControlSubjInt("Key vol", &cfg_sm.p_key_vol)},
    {CTRL_QSK_TIME, new ControlSubjInt("QSK time", &cfg_sm.p_qsk_time)},
    {CTRL_KEY_RATIO, new ControlSubjFloat("Key ratio", &cfg_sm.p_key_ratio)},
    {CTRL_CW_DECODER, new ControlSubjOnOff("CW decoder", &cfg_sm.p_cw_decoder)},
    {CTRL_CW_TUNE, new ControlSubjOnOff("CW tuner", &cfg_sm.p_cw_tune)},
    {CTRL_CW_DECODER_SNR, new ControlSubjFloat("CW decoded snr", &cfg_sm.p_cw_decoder_snr)},
    {CTRL_CW_PEAK_ON, new ControlSubjOnOff("CW peak", &cfg_sm.p_cw_peak_on)},
    {CTRL_CW_PEAK_Q, new ControlSubjInt("CW peak Q", &cfg_sm.p_cw_peak_q)},
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
    vol_knob_info->set_edit_mode(true);

    mfk_info = lv_label_create(parent);
    lv_obj_add_style(mfk_info, &knobs_style, 0);
    lv_obj_set_pos(mfk_info, x_static, y - KNOBS_HEIGHT * 1);
    lv_label_set_recolor(mfk_info, true);
    lv_label_set_text(mfk_info, "");
    mfk_knob_info->set_edit_mode(true);

    cfg_sm.p_knob_info.subscribe_delayed_and_notify(on_knob_info_enabled_change, nullptr);
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
    enabled = cfg_sm.p_knob_info.get();
}
