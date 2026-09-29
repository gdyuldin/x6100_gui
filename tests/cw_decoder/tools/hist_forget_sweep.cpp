// Measurement tool for the histogram forgetting factor HIST_FORGET.
//
// Not a test: it is not registered with add_test (see
// tests/cw_decoder/CMakeLists.txt) so ctest stays fast. Run it manually:
//   cmake --build build_test --target hist_forget_sweep
//   ./build_test/tests/cw_decoder/hist_forget_sweep
//
// It drives cw::TimeClassifier directly with exact frame durations (no DSP) so
// only the adaptation is measured, and reports for each candidate value:
//   * the number of intervals needed to re-lock after a 25 -> 15 WPM change
//     (|u/u_true - 1| < 5% held for several intervals);
//   * the relative standard deviation of the unit in steady state under +/-1
//     frame jitter (stability);
//   * the fraction of correctly classified dot/dash tokens.
//
// A token is emitted on the frame that closes its interval, so they are
// collected over the whole stream rather than per edge.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "time_classifier.h"
#include "time_classifier_test_access.h"

using Access = cw::TimeClassifierTestAccess;

namespace {

constexpr float WPM_25_UNIT_MS = 1200.0f / 25.0f; // 48 ms = 6 frames
constexpr float WPM_15_UNIT_MS = 1200.0f / 15.0f; // 80 ms = 10 frames

uint32_t rng_state = 0x12345678u;

int jitter_frame(int frames) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return frames + (static_cast<int>((rng_state >> 16) & 1u) * 2 - 1);
}

// Feeds whole-frame intervals, so every crossing is at a hop boundary (dt 0).
void feed(cw::TimeClassifier &tc, bool &state, bool on, int frames, std::vector<cw::Token> *out) {
    for (int i = 0; i < frames; ++i) {
        const int edge    = (on != state) ? (on ? +1 : -1) : 0;
        state             = on;
        const cw::Token t = tc.feed(edge, 0.0f);
        if (out != nullptr && t != cw::CW_NONE)
            out->push_back(t);
    }
}

struct SweepResult {
    float hist_forget;
    int   converge_intervals;
    float unit_std_frac;
    float token_accuracy;
};

SweepResult sweep(float hist_forget, int reps) {
    cw::TimeClassifier tc;
    Access::set_hist_forget(tc, hist_forget);
    bool state = false;

    // Warm-up at 25 WPM, dots only.
    for (int rep = 0; rep < 40; ++rep) {
        feed(tc, state, true, 6, nullptr);
        feed(tc, state, false, 6, nullptr);
    }

    // Speed change 25 -> 15 WPM: count intervals until the unit stays within 5%.
    int converge_intervals = -1;
    int hold               = 0;
    int interval           = 0;
    for (int rep = 0; rep < reps && converge_intervals < 0; ++rep) {
        feed(tc, state, true, 10, nullptr);  // dot
        feed(tc, state, false, 10, nullptr); // element space
        feed(tc, state, true, 30, nullptr);  // dash
        feed(tc, state, false, 10, nullptr); // element space
        for (int k = 0; k < 4; ++k) {
            ++interval;
            const float unit = Access::unit_ms(tc);
            if (std::fabs(unit / WPM_15_UNIT_MS - 1.0f) < 0.05f) {
                if (++hold >= 3 && converge_intervals < 0)
                    converge_intervals = interval;
            } else {
                hold = 0;
            }
        }
    }

    // Steady state at 25 WPM with +/-1 frame jitter: unit stability and token
    // accuracy (dots and dashes of 6 and 18 frames).
    std::vector<float>     unit_samples;
    std::vector<cw::Token> tokens;
    for (int rep = 0; rep < 200; ++rep) {
        const int mark_frames = jitter_frame(6);
        const int dash_frames = jitter_frame(18);
        feed(tc, state, true, mark_frames, &tokens);
        feed(tc, state, false, jitter_frame(6), &tokens);
        feed(tc, state, true, dash_frames, &tokens);
        feed(tc, state, false, jitter_frame(6), &tokens);
        if (rep >= 50) {
            unit_samples.push_back(Access::unit_ms(tc));
            if (rep == 50)
                tokens.clear(); // drop tokens emitted before the measured window
        }
    }

    float mean = 0.0f;
    for (float value : unit_samples)
        mean += value;
    if (!unit_samples.empty())
        mean /= static_cast<float>(unit_samples.size());
    float var = 0.0f;
    for (float value : unit_samples)
        var += (value - mean) * (value - mean);
    if (unit_samples.size() > 1)
        var /= static_cast<float>(unit_samples.size());

    int dots    = 0;
    int dashes  = 0;
    int measured = static_cast<int>(unit_samples.size());
    for (cw::Token t : tokens) {
        if (t == cw::CW_DOT)
            ++dots;
        else if (t == cw::CW_DASH)
            ++dashes;
    }
    float accuracy = 0.0f;
    if (measured > 0) {
        accuracy = static_cast<float>(dots + dashes) / static_cast<float>(2 * measured);
        if (accuracy > 1.0f)
            accuracy = 1.0f;
    }

    SweepResult result;
    result.hist_forget        = hist_forget;
    result.converge_intervals = converge_intervals;
    result.unit_std_frac      = (mean > 0.0f) ? std::sqrt(var) / mean : 0.0f;
    result.token_accuracy     = accuracy;
    return result;
}

} // namespace

int main() {
    const float candidates[] = {0.80f, 0.85f, 0.90f, 0.93f, 0.95f, 0.97f};

    std::printf("%-12s %-20s %-16s %-14s\n", "HIST_FORGET", "converge(intervals)", "unit_std(rel)", "token_acc");
    for (float candidate : candidates) {
        const SweepResult result = sweep(candidate, 400);
        std::printf("%-12.2f %-20d %-16.4f %-14.3f\n", result.hist_forget, result.converge_intervals,
                    result.unit_std_frac, result.token_accuracy);
    }
    return 0;
}
