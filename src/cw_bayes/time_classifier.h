#pragma once

#include <array>

#include "cw_config.h"

namespace cw {

// Grants the tests access to the adaptation state without widening the class
// interface. Defined only in the test tree (tests/cw_bayes/).
struct TimeClassifierTestAccess;

// Duration-histogram size and resolution. The resolution is deliberately finer
// than the BIN_SIZE_MS frame duration: the histogram holds closed-interval
// durations (marks and spaces), not whole frames.
constexpr size_t HIST_BINS   = 300;
constexpr float  HIST_BIN_MS = BIN_SIZE_MS / 2.0f;

// Tracked unit (dot length) bounds; the default is ~20 WPM.
constexpr float UNIT_MS_DEFAULT = static_cast<float>(WPM_K) / 20.0f;
constexpr float UNIT_MS_MIN     = static_cast<float>(WPM_K) / 50.0f; // 50 WPM
constexpr float UNIT_MS_MAX     = static_cast<float>(WPM_K) / 10.0f; // 10 WPM

// Morse boundaries as multiples of the unit (dot/dash 2u, element/letter 2u,
// letter/word 5u).
constexpr float DOT_DASH_TH_K    = 2.0f;
constexpr float ELEM_LETTER_TH_K = 2.0f;
constexpr float LETTER_WORD_TH_K = 5.0f;

// Histogram leak; measured by tests/cw_bayes/tools/hist_forget_sweep.cpp.
constexpr float HIST_FORGET = 0.9f;

// Splits a keyed envelope into Morse intervals. The unit is tracked from a
// single histogram of all closed intervals (marks and spaces together): a mark
// is stretched by the level crossing while the following space is shortened by
// the same amount, so the two merge into one peak at the true unit. The unit is
// the centre of mass of the lowest cluster, not a discrete bin, so a multimodal
// cluster (dots, element spaces, jitter) averages out. An asymmetric Schmitt
// decides ON/OFF: acquisition is level-independent (absolute gate), release is
// level-relative, and a half-confirmed transition is cancelled only by the
// opposite crossing, so brief glitches are merged back into the current symbol.
// Each edge is interpolated between the two frames that bracket its own Schmitt
// crossing (front edge at the arm level, back edge at the release level).
class TimeClassifier {
  public:
    Token feed_frame(float abs_llr, float norm_db, bool ref_valid);
    float get_current_wpm() const;
    bool  is_signal_active() const;

    // Drops the learned histogram and restores the default unit.
    void reset_speed();

  private:
    friend struct TimeClassifierTestAccess;

    void  update_unit();
    void  update_boundaries();
    void  decay_histogram(std::array<float, HIST_BINS> &hist);
    void  add_sample(float duration_ms, float weight);
    float estimate_unit() const;
    Token classify_and_update(bool closed_on, float closed_duration_ms, float closed_probability);
    Token handle_off_idle();

    std::array<float, HIST_BINS> hist_{};

    // In-range intervals recorded since the histogram was last rebuilt; gates the
    // relock so a sparse post-rebuild histogram cannot start a speed hunt.
    int hist_samples_ = 0;

    // Committed level: the value is_signal_active() reports.
    bool  is_now_on_           = false;
    float current_duration_ms_ = 0.0f;
    float accumulated_llr_     = 0.0f;
    int   frame_count_         = 0;

    // Deferred transition: candidate level and the interval it would close.
    bool  pending_active_      = false;
    bool  pending_level_       = false;
    float pending_duration_ms_ = 0.0f;
    float pending_llr_         = 0.0f;
    int   pending_count_       = 0;
    float closed_duration_ms_  = 0.0f;
    float closed_probability_  = 0.0f;

    float off_idle_ms_ = 0.0f;
    // Latches the one-shot idle word space so a long silence is reported once.
    bool  idle_reported_ = false;

    // Previous frame envelope, for the sub-frame edge interpolation.
    float prev_norm_db_    = 0.0f;
    bool  prev_norm_valid_ = false;

    float unit_ms_ = UNIT_MS_DEFAULT;
    // Fast re-lock: a candidate far from the current unit is committed after it
    // repeats for SPEED_CONFIRM_FRAMES, which rebuilds the histogram from the
    // new speed instead of dragging the old one across.
    float candidate_unit_ms_ = -1.0f;
    int   relock_frames_     = 0;
    float hist_forget_       = HIST_FORGET;

    // Decision boundaries derived from unit_ms_ (start values ~20 WPM).
    float threshold_dot_dash_    = DOT_DASH_TH_K * UNIT_MS_DEFAULT;
    float threshold_elem_letter_ = ELEM_LETTER_TH_K * UNIT_MS_DEFAULT;
    float threshold_letter_word_ = LETTER_WORD_TH_K * UNIT_MS_DEFAULT;
};

} // namespace cw
