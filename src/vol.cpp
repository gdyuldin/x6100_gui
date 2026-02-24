/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "vol.h"
#include "helpers.h"
#include "util.h"
#include "knobs.h"
#include "controls.h"

extern "C" {
    #include "msg.h"
    #include "radio.h"
    #include "main.h"
    #include "params/params.h"
    #include "voice.h"
    #include "cfg/mode.h"
}

static cfg_ctrl_t   vol_ctrl = CTRL_VOL;

void vol_init() {
    // Find first available config
    vol_ctrl = controls_encoder_get_next(ENCODER_BIND_VOL, vol_ctrl, 0);
    knobs_set_vol_param(vol_ctrl);
}


void vol_update(int16_t diff) {

    std::string msg(64, '\0');
    controls_encoder_update(vol_ctrl, diff, msg);

    uint32_t    color = vol->state == VOL_STATE_EDIT ? 0xFFFFFF : 0xBBBBBB;

    if (!knobs_visible()) {
        msg_update_text_fmt("#%3X %s", color, msg.c_str());
    }
}

void vol_change_ctrl(int16_t dir) {
    if (dir == 0) {
        LV_LOG_ERROR("Unknown direction for change control (0)");
        return;
    }
    vol_ctrl = controls_encoder_get_next(ENCODER_BIND_VOL, vol_ctrl, dir);
    knobs_set_vol_param(vol_ctrl);
    control_name_say(vol_ctrl);
    vol_update(0);
}

void vol_set_ctrl(cfg_ctrl_t ctrl) {
    vol_ctrl = ctrl;
    vol->state = VOL_STATE_EDIT;
    knobs_set_vol_param(vol_ctrl);
    knobs_set_vol_state(true);
    control_name_say(ctrl);
    vol_update(0);
}
