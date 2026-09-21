/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <array>

#include "cw_types.h"

namespace cw {

// Robust histogram of mark/space durations with a Schmitt trigger, rectangular
// convolution and centre-of-mass peak refinement. It adapts the dot/dash and
// element/letter/word boundaries to the sender's speed and emits one Token at
// every physical transition.
class TimeClassifier {
  public:
    Token feed_frame_llr(float current_frame_llr);
    float get_current_wpm() const;

  private:
    float find_precise_peak(const std::array<float, HIST_BINS> &hist, size_t start_bin, size_t end_bin) const;
    void  update_thresholds();
    void  add_to_histogram(std::array<float, HIST_BINS> &hist, int duration_ms, float weight);

    std::array<float, HIST_BINS> hist_on_{};
    std::array<float, HIST_BINS> hist_off_{};

    bool  is_now_on_           = false;
    int   current_duration_ms_ = 0;
    float accumulated_llr_     = 0.0f;
    int   frame_count_         = 0;
    int   sample_count_        = 0;

    // Adaptive decision boundaries (start values for ~20 WPM)
    int threshold_dot_dash_    = 160;
    int threshold_elem_letter_ = 160;
    int threshold_letter_word_ = 480;
};

} // namespace cw
