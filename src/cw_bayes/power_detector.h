/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

namespace cw {

// Bayesian power detector: turns the peak/noise power pair of one frame into a
// log-likelihood ratio (LLR) for the Schmitt trigger. Memoryless, so a single
// frame above threshold is enough to switch state.
class PowerDetector {
  public:
    void set_threshold_db(float db_val);
    void reset();

    float get_raw_llr(float peak_power, float noise_power);
    float get_llr() const;

  private:
    float llr_               = -3.0f;
    float snr_threshold_lin_ = 10.0f; // Default 10 dB
};

} // namespace cw
