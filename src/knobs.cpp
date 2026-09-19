/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 *  Copyright (c) 2025 Georgy Dyuldin R2RFE
 */


#include "knobs.h"

#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include <memory>

#include "globals.h"
#include "buttons.h"
#include "cfg/cfg_api.h"
#include "format.h"
#include "pubsub_ids.h"

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

static struct {
    bool panel;
    bool dialog;
    bool enabled;
} visibility_state;

/* Knob items classes - for each of possible knob action */

struct Control {
    const char *name;

    Control(const char *name) : name(name) {};
    virtual ~Control() = default;

    virtual std::string to_str()=0;

    virtual ObserverDelayed* subscribe(observer_cb cb, void *user_data) {
        return nullptr;
    }

  protected:
    static std::string float_to_str(float val, std::string fmt) {
        size_t len = snprintf(nullptr, 0, fmt.c_str(), val);
        std::string buf(len + 1, '\0');
        snprintf(buf.data(), buf.size(), fmt.c_str(), val);
        buf.resize(len);
        return buf;
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
    std::string to_str() { return std::string(format_comp_str_get(subj->get())); }
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
static void update_visibility();


static std::map<int, std::unique_ptr<Control>> make_controls() {
    std::map<int, std::unique_ptr<Control>> controls;

    auto add = [&controls](int key, Control *item) {
        controls.emplace(key, std::unique_ptr<Control>(item));
    };

    add(CTRL_VOL, new ControlSubjInt("Volume", cfg.volume()));
    add(CTRL_SQL, new ControlSubjInt("Voice SQL", cfg.squelch()));
    add(CTRL_RFG, new ControlSubjInt("RF gain", cfg.rfgain()));
    add(CTRL_FILTER_LOW, new ControlSubjInt("Filter low", cfg.filter.low()));
    add(CTRL_FILTER_HIGH, new ControlSubjInt("Filter high", cfg.filter.high()));
    add(CTRL_FILTER_BW, new ControlSubjInt("Filter bw", cfg.filter.bw()));
    add(CTRL_PWR, new ControlSubjFloat("Power", cfg.pwr(), "%0.1f"));
    add(CTRL_MIC, new ControlSubjChoices("MIC", cfg.mic(), {"Built-In", "Handle", "Auto"}));
    add(CTRL_HMIC, new ControlSubjInt("H-MIC gain", cfg.hmic()));
    add(CTRL_IMIC, new ControlSubjInt("I-MIC gain", cfg.imic()));
    add(CTRL_MONI, new ControlSubjInt("Moni level", cfg.moni()));
    add(CTRL_SPECTRUM_FACTOR, new ControlSubjInt("Zoom", cfg.mode.zoom()));
    add(CTRL_COMP, new ControlComp("Compressor", cfg.dsp.comp()));

    add(CTRL_VOX_ON, new ControlSubjOnOff("VOX", cfg.vox.on()));
    add(CTRL_VOX_GAIN, new ControlSubjInt("VOX gain", cfg.vox.gain()));
    add(CTRL_VOX_AG, new ControlSubjInt("VOX a-gain", cfg.vox.ag()));
    add(CTRL_VOX_DELAY, new ControlSubjInt("VOX delay", cfg.vox.delay()));

    add(CTRL_ANT, new ControlSubjInt("Ant", cfg.ant_id()));
    add(CTRL_RIT, new ControlSubjInt("RIT", cfg.rit()));
    add(CTRL_XIT, new ControlSubjInt("XIT", cfg.xit()));
    add(CTRL_IF_SHIFT, new ControlSubjInt("IF shift", cfg.band.if_shift()));

    add(CTRL_DNF, new ControlSubjOnOff("Notch filter", cfg.dsp.dnf()));
    add(CTRL_DNF_CENTER, new ControlSubjInt("DNF center", cfg.dsp.dnf_center()));
    add(CTRL_DNF_WIDTH, new ControlSubjInt("DNF width", cfg.dsp.dnf_width()));
    add(CTRL_DNF_AUTO, new ControlSubjOnOff("DNF auto", cfg.dsp.dnf_auto()));
    add(CTRL_NB, new ControlSubjOnOff("Noise blanker", cfg.dsp.nb()));
    add(CTRL_NB_LEVEL, new ControlSubjInt("NB level", cfg.dsp.nb_level()));
    add(CTRL_NB_WIDTH, new ControlSubjInt("NB width", cfg.dsp.nb_width()));
    add(CTRL_NR, new ControlSubjOnOff("Noise reduction", cfg.dsp.nr()));
    add(CTRL_NR_LEVEL, new ControlSubjInt("NR level", cfg.dsp.nr_level()));

    add(CTRL_AGC_HANG, new ControlSubjOnOff("AGC hang", cfg.agc.hang()));
    add(CTRL_AGC_KNEE, new ControlSubjInt("AGC knee", cfg.agc.knee()));
    add(CTRL_AGC_SLOPE, new ControlSubjInt("AGC slope", cfg.agc.slope()));

    add(CTRL_KEY_SPEED, new ControlSubjInt("Key speed", cfg.cw.key_speed()));
    add(CTRL_KEY_TRAIN, new ControlSubjOnOff("Key train", cfg.cw.key_train()));
    add(CTRL_KEY_MODE, new ControlSubjChoices("Key mode", cfg.cw.key_mode(), {"Manual", "Auto-L", "Auto-R"}));
    add(CTRL_IAMBIC_MODE, new ControlSubjChoices("Iambic mode", cfg.cw.iambic_mode(), {"A", "B"}));
    add(CTRL_KEY_TONE, new ControlSubjInt("Key tone", cfg.cw.key_tone()));
    add(CTRL_KEY_VOL, new ControlSubjInt("Key vol", cfg.cw.key_vol()));
    add(CTRL_QSK_TIME, new ControlSubjInt("QSK time", cfg.cw.qsk_time()));
    add(CTRL_KEY_RATIO, new ControlSubjFloat("Key ratio", cfg.cw.key_ratio()));
    add(CTRL_CW_DECODER, new ControlSubjOnOff("CW decoder", cfg.cw.decoder()));
    add(CTRL_CW_TUNE, new ControlSubjOnOff("CW tuner", cfg.cw.tune()));
    add(CTRL_CW_DECODER_SNR, new ControlSubjFloat("CW decoded snr", cfg.cw.decoder_snr()));
    add(CTRL_CW_PEAK_ON, new ControlSubjOnOff("CW peak", cfg.cw.peak_on()));
    add(CTRL_CW_PEAK_Q, new ControlSubjInt("CW peak Q", cfg.cw.peak_q()));
    // add(MFK_RTTY_RATE, Control("RTTY rate", []() { return to_str((float)param_i_get(cfg.rtty.rate()) / 100.0f, "%0.2f"); }));
    // add(MFK_RTTY_SHIFT, Control("RTTY shift", []() { return std::to_string(param_i_get(cfg.rtty.shift())); }));
    // add(MFK_RTTY_CENTER, Control("RTTY center", []() { return std::to_string(param_i_get(cfg.rtty.center())); }));
    // add(MFK_RTTY_REVERSE, Control("RTTY reverse", []() { return std::string(param_i_get(cfg.rtty.reverse()) ? "On" : "Off"); }));

    return controls;
}

static const std::map<int, std::unique_ptr<Control>> controls = make_controls();

static lv_obj_t *vol_info;

static lv_obj_t *mfk_info;

static std::unique_ptr<KnobInfo> vol_knob_info = std::make_unique<KnobInfo>(&vol_info, LV_SYMBOL_UP);
static std::unique_ptr<KnobInfo> mfk_knob_info = std::make_unique<KnobInfo>(&mfk_info, LV_SYMBOL_DOWN);


void knobs_init(lv_obj_t * parent) {
    // Basic positon calculation
    uint16_t y = SCREEN_HEIGHT - BTN_HEIGHT - 5;
    uint16_t x_static = KNOBS_PADDING;
    uint16_t x_dynamic = x_static  + KNOBS_STATIC_WIDTH + KNOBS_PADDING;

    // Init
    vol_info = lv_label_create(parent);
    lv_obj_add_style(vol_info, &style.knobs, 0);
    lv_obj_set_pos(vol_info, x_static, y - KNOBS_HEIGHT * 2);
    lv_label_set_recolor(vol_info, true);
    lv_label_set_text(vol_info, "");
    vol_knob_info->set_edit_mode(true);

    mfk_info = lv_label_create(parent);
    lv_obj_add_style(mfk_info, &style.knobs, 0);
    lv_obj_set_pos(mfk_info, x_static, y - KNOBS_HEIGHT * 1);
    lv_label_set_recolor(mfk_info, true);
    lv_label_set_text(mfk_info, "");
    mfk_knob_info->set_edit_mode(true);

    cfg.ui.knob_info()->subscribe_delayed_and_notify(on_knob_info_enabled_change);

    lv_msg_subscribe(MSG_DIALOG_START, [](void*, lv_msg_t*){
        visibility_state.dialog = true;
        update_visibility();
    }, NULL);
    lv_msg_subscribe(MSG_DIALOG_STOP, [](void*, lv_msg_t*){
        visibility_state.dialog = false;
        update_visibility();
    }, NULL);

    lv_msg_subscribe(MSG_PANEL_SHOW, [](void*, lv_msg_t*){
        visibility_state.panel = true;
        update_visibility();
    }, NULL);
    lv_msg_subscribe(MSG_PANEL_HIDE, [](void*, lv_msg_t*){
        visibility_state.panel = false;
        update_visibility();
    }, NULL);
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
        item = controls.at(control).get();
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
        item = controls.at(control).get();
    } catch (const std::out_of_range &ex) {
        LV_LOG_WARN("MFK Control %d is unknown, skip, %s", control, ex.what());
        return;
    }
    mfk_knob_info->set_ctrl(item);
}


static void on_knob_info_enabled_change(Subject *subj, void *user_data) {
    visibility_state.enabled = cfg.ui.knob_info()->get();
    update_visibility();
}

static void update_visibility() {
    if (visibility_state.enabled && !visibility_state.dialog && !visibility_state.panel) {
        lv_obj_clear_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    }
}
