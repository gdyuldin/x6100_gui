/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "mfk.h"

#include "util.hpp"
#include "cw.h"
#include "voice.h"
#include "util.h"
#include "knobs.h"
#include "dsp.h"
#include "controls.h"

#include <vector>

extern "C" {
    #include "params/params.h"
    #include "spectrum.h"
    #include "waterfall.h"
    #include "msg.h"
    #include "radio.h"
    #include "info.h"
    #include "backlight.h"
    #include "cw_tune_ui.h"
    #include "band_info.h"
    #include "pubsub_ids.h"
    #include "meter.h"

    #include "lvgl/lvgl.h"
}

template <typename T> static T loop_items(std::vector<T> items, T cur, bool next);

mfk_state_t  mfk_state = MFK_STATE_EDIT;
cfg_ctrl_t   mfk_ctrl = CTRL_SPECTRUM_FACTOR;

void mfk_init() {
    // Find first available config
    mfk_ctrl = controls_encoder_get_next(ENCODER_BIND_MFK, mfk_ctrl, 0);
    knobs_set_mfk_param(mfk_ctrl);
}

void mfk_update(int16_t diff) {
    std::string msg(64, '\0');
    controls_encoder_update(mfk_ctrl, diff, msg);

    uint32_t    color = mfk_state == MFK_STATE_EDIT ? 0xFFFFFF : 0xBBBBBB;

    if (!knobs_visible()) {
        msg_update_text_fmt("#%3X %s", color, msg.c_str());
    }
}

void mfk_change_ctrl(int16_t dir) {
    if (dir == 0) {
        LV_LOG_ERROR("Unknown direction for change control (0)");
        return;
    }
    mfk_ctrl = controls_encoder_get_next(ENCODER_BIND_MFK, mfk_ctrl, dir);
    knobs_set_mfk_param(mfk_ctrl);
    control_name_say(mfk_ctrl);
    mfk_update(0);
}

void mfk_set_ctrl(cfg_ctrl_t ctrl) {
    mfk_ctrl = ctrl;
    mfk_state = MFK_STATE_EDIT;
    knobs_set_mfk_state(true);
    knobs_set_mfk_param(mfk_ctrl);
    control_name_say(ctrl);
    mfk_update(0);
}
