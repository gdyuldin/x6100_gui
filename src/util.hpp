/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <cstdint>
#include <vector>

#include "cfg/encoder_bind_types.h"

cfg_ctrl_t loop_modes(int16_t dir, cfg_ctrl_t mode, const uint64_t mask, const std::vector<cfg_ctrl_t> all_modes);
