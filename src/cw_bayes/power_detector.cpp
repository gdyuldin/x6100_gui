/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "power_detector.h"

#include <cmath>

namespace cw {

void PowerDetector::set_threshold_db(float db_val) {
    // +1.7 dB micro-correction compensating the Hann window ENBW
    snr_threshold_lin_ = std::pow(10.0f, (db_val + 1.7f) / 10.0f);
}

void PowerDetector::reset() {
    llr_ = -3.0f;
}

float PowerDetector::get_raw_llr(float peak_power, float noise_power) {
    if (noise_power < 1e-10f)
        noise_power = 1e-10f;

    // Strong click guard (SNR > 40 dB): force deep silence immediately
    if ((peak_power / noise_power) > 10000.0f) {
        llr_ = -4.0f;
        return llr_;
    }

    float expected_signal = noise_power * snr_threshold_lin_;

    // Fast analytical LLR solution for the exponential power distribution
    llr_ = (peak_power / noise_power) - (std::fabs(peak_power - expected_signal) / noise_power);
    return llr_;
}

float PowerDetector::get_llr() const {
    return llr_;
}

} // namespace cw
