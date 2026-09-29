// Test-only access to the private adaptation state of cw::TimeClassifier. The
// class declares `friend struct TimeClassifierTestAccess;` (see
// time_classifier.h); this header is never compiled into the production module.
#pragma once

#include <array>
#include <cstddef>

#include "time_classifier.h"

namespace cw {

struct TimeClassifierTestAccess {
    static float unit_ms(const TimeClassifier &tc) { return tc.unit_ms_; }

    static float current_duration_ms(const TimeClassifier &tc) { return tc.current_duration_ms_; }

    static const std::array<float, HIST_BINS> &hist(const TimeClassifier &tc) { return tc.hist_; }

    static void set_unit_ms(TimeClassifier &tc, float value) {
        tc.unit_ms_ = value;
        tc.update_boundaries();
    }

    static void set_hist_forget(TimeClassifier &tc, float value) { tc.hist_forget_ = value; }

    static void clear_histograms(TimeClassifier &tc) {
        tc.hist_.fill(0.0f);
        tc.hist_samples_ = 0;
    }

    static void set_hist(TimeClassifier &tc, size_t bin, float value) { tc.hist_[bin] = value; }

    static void add_sample(TimeClassifier &tc, float duration_ms) { tc.add_sample(duration_ms); }
};

} // namespace cw
