#pragma once

#include <array>

#include "cw_types.h"

namespace cw {

constexpr float DOT_DASH_TH_K = 1.55f;

// Robust histogram of mark/space durations with a Schmitt trigger, rectangular
// convolution and centre-of-mass peak refinement. It adapts the dot/dash and
// element/letter/word boundaries to the sender's speed and emits one Token at
// every physical transition.
class TimeClassifier {
  public:
    Token feed_frame_llr(float current_frame_llr);
    float get_current_wpm() const;
    bool is_signal_active() const;

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
    int   idle_frames_counter  = 0;

    // Adaptive decision boundaries (start values for ~20 WPM)
    float threshold_dot_dash_    = WPM_K * DOT_DASH_TH_K / 20;
    float threshold_elem_letter_ = WPM_K * DOT_DASH_TH_K / 20;
    float threshold_letter_word_ = WPM_K * 6.0f / 20;
};

} // namespace cw
