#include "time_classifier.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cw {

namespace {

// Triangular spread half-width, in histogram bins: a closed interval lands in a
// band of +/-SPREAD_HALF_BINS around its duration instead of a single bin, so a
// mark and its shortened space (offset by ~one frame each way) overlap even when
// the frame quantization puts them a bin apart.
constexpr int SPREAD_HALF_BINS = 4;

// A local maximum counts as a peak only above this fraction of the histogram
// maximum, so the spread tails do not spawn phantom peaks.
constexpr float PEAK_PROMINENCE_FRAC = 0.08f;

// A peak is "above" the lowest cluster when its duration reaches 1.7x the
// lowest peak (the true next cluster is a letter space at ~3u or a dash at 3u).
constexpr float CLUSTER_SEP_K = 1.7f;

// Without a second peak the centroid stops at this multiple of the lowest peak,
// so the spread tail alone cannot drag the unit past ~2.2u.
constexpr float CENTROID_CAP_K = 2.2f;

// A candidate farther than this fraction from the current unit counts as a new
// speed and needs SPEED_CONFIRM_FRAMES repeats before the histogram is rebuilt.
constexpr float SPEED_RELOCK_FRAC    = 0.25f;
constexpr int   SPEED_CONFIRM_FRAMES = 3;

// A rebuild empties the histogram, so the first estimates afterwards are driven
// by one or two samples (the first dash closes before any element space) and
// report the dash length as a "far" candidate. Without a maturity gate that
// candidate relocks again, the next samples pull back to the true unit, and the
// estimator hunts between the two forever (dash-heavy text: 29<->10 WPM). Ignore
// far candidates until the rebuilt histogram carries enough intervals for both
// the unit and the dash cluster to be represented.
constexpr int MIN_RELOCK_SAMPLES = 8;

// Smoothing of the unit between updates.
constexpr float UNIT_EMA_GAIN = 0.3f;

// End of a message: an OFF stretch with no following mark to close it. After
// this many element/letter units of silence the pending character is flushed
// with one word space; the floor keeps a slow station from waiting too long.
constexpr float IDLE_WORD_K      = 5.0f;
constexpr float IDLE_WORD_MIN_MS = 200.0f;

} // namespace

void TimeClassifier::update_boundaries() {
    threshold_dot_dash_    = DOT_DASH_TH_K * unit_ms_;
    threshold_elem_letter_ = ELEM_LETTER_TH_K * unit_ms_;
    threshold_letter_word_ = LETTER_WORD_TH_K * unit_ms_;
}

void TimeClassifier::update_unit() {
    float candidate = estimate_unit();
    if (candidate < 0.0f)
        return;
    candidate = std::clamp(candidate, UNIT_MS_MIN, UNIT_MS_MAX);

    if (std::fabs(candidate - unit_ms_) <= SPEED_RELOCK_FRAC * unit_ms_) {
        unit_ms_           = std::clamp(unit_ms_ + UNIT_EMA_GAIN * (candidate - unit_ms_), UNIT_MS_MIN, UNIT_MS_MAX);
        candidate_unit_ms_ = -1.0f;
        relock_frames_     = 0;
    } else {
        // A far candidate is a speed change: commit only after it repeats, then
        // drop the stale histogram so the estimate is not dragged by two speeds.
        // A freshly rebuilt histogram is too sparse to tell a dash cluster from
        // a real speed change, so wait for it to mature first.
        if (hist_samples_ < MIN_RELOCK_SAMPLES) {
            candidate_unit_ms_ = -1.0f;
            relock_frames_     = 0;
            return;
        }
        const bool repeats =
            relock_frames_ > 0 && std::fabs(candidate - candidate_unit_ms_) <= SPEED_RELOCK_FRAC * candidate_unit_ms_;
        candidate_unit_ms_ = candidate;
        relock_frames_     = repeats ? relock_frames_ + 1 : 1;
        if (relock_frames_ >= SPEED_CONFIRM_FRAMES) {
            hist_.fill(0.0f);
            hist_samples_      = 0;
            unit_ms_           = candidate;
            candidate_unit_ms_ = -1.0f;
            relock_frames_     = 0;
        }
    }
    update_boundaries();
}

void TimeClassifier::decay_histogram(std::array<float, HIST_BINS> &hist) {
    for (float &value : hist)
        value *= hist_forget_;
}

void TimeClassifier::add_sample(float duration_ms) {
    // The leak runs once per sample regardless of whether the sample lands in
    // range: an out-of-range interval must still age the history.
    decay_histogram(hist_);
    if (duration_ms <= 0.0f)
        return;

    const int center = static_cast<int>(std::lround(duration_ms / HIST_BIN_MS));
    if (center < 0 || center >= static_cast<int>(HIST_BINS))
        return;

    // In-range intervals are the ones that make the histogram mature enough to
    // trust a far speed candidate (see MIN_RELOCK_SAMPLES).
    hist_samples_++;

    // Triangular kernel, normalised over the in-range bins so every sample adds
    // unit mass and the wider kernel does not bias the centroid.
    float norm = 0.0f;
    for (int k = -SPREAD_HALF_BINS; k <= SPREAD_HALF_BINS; ++k) {
        const int bin = center + k;
        if (bin >= 0 && bin < static_cast<int>(HIST_BINS))
            norm += static_cast<float>(SPREAD_HALF_BINS + 1 - std::abs(k));
    }
    if (norm <= 0.0f)
        return;
    for (int k = -SPREAD_HALF_BINS; k <= SPREAD_HALF_BINS; ++k) {
        const int bin = center + k;
        if (bin < 0 || bin >= static_cast<int>(HIST_BINS))
            continue;
        hist_[bin] += static_cast<float>(SPREAD_HALF_BINS + 1 - std::abs(k)) / norm;
    }
}

// Unit estimate: the centre of mass of the lowest peak's cluster.
//
// Marks (stretched by the crossing) and spaces (shortened by the same amount)
// merge into one peak at the true unit, so a discrete bin (strongest or lowest)
// would misread a multimodal cluster. The low cluster is cut at the first
// sufficiently separated higher peak (a letter space ~3u or a dash 3u), or at
// CENTROID_CAP_K*u when there is none, and the centroid is taken over
// [0, valley] where the valley is the dip before the cut.
float TimeClassifier::estimate_unit() const {
    float max_val = 0.0f;
    for (float value : hist_)
        max_val = std::max(max_val, value);
    if (max_val <= 0.0f)
        return -1.0f;

    const int   bins     = static_cast<int>(HIST_BINS);
    const float min_prom = PEAK_PROMINENCE_FRAC * max_val;

    int p1 = -1;
    for (int i = 0; i < bins; ++i) {
        const float left  = (i > 0) ? hist_[i - 1] : 0.0f;
        const float right = (i + 1 < bins) ? hist_[i + 1] : 0.0f;
        if (hist_[i] >= min_prom && hist_[i] > left && hist_[i] >= right) {
            p1 = i;
            break;
        }
    }
    if (p1 < 0)
        return -1.0f;

    const float p1_ms = static_cast<float>(p1) * HIST_BIN_MS;

    int cut = -1;
    for (int i = p1 + 1; i < bins; ++i) {
        const float left  = hist_[i - 1];
        const float right = (i + 1 < bins) ? hist_[i + 1] : 0.0f;
        if (hist_[i] >= min_prom && hist_[i] > left && hist_[i] >= right &&
            static_cast<float>(i) * HIST_BIN_MS >= CLUSTER_SEP_K * p1_ms) {
            cut = i;
            break;
        }
    }
    if (cut < 0) {
        cut = static_cast<int>(std::lround(CENTROID_CAP_K * p1_ms / HIST_BIN_MS));
        if (cut >= bins)
            cut = bins - 1;
    }
    if (cut <= p1)
        cut = std::min(p1 + 1, bins - 1);

    int valley = p1;
    for (int i = p1; i <= cut; ++i) {
        if (hist_[i] < hist_[valley])
            valley = i;
    }

    float mass = 0.0f;
    float sum  = 0.0f;
    for (int i = 0; i <= valley; ++i) {
        mass += hist_[i];
        sum += static_cast<float>(i) * hist_[i];
    }
    if (mass <= 0.0f)
        return -1.0f;
    return (sum / mass) * HIST_BIN_MS;
}

void TimeClassifier::on_retune() {
    // Keep is_now_on_ so the classifier stays in step with the Detector, which
    // reports the next crossing itself; only the in-flight interval is dropped.
    // A small timing error when the station changes is accepted. The learned
    // speed (unit_ms_ and hist_) is kept: a retune is almost always the same
    // operator resuming.
    current_duration_ms_ = 0.0f;
    idle_reported_       = false;
}

// Runs the histogram update and token classification for one closed interval.
Token TimeClassifier::classify_and_update(bool closed_on, float closed_duration_ms) {
    Token token;
    if (closed_on) {
        add_sample(closed_duration_ms);
        token = (closed_duration_ms < threshold_dot_dash_) ? CW_DOT : CW_DASH;
    } else {
        add_sample(closed_duration_ms);
        if (closed_duration_ms < threshold_elem_letter_)
            token = CW_ELEMENT_SPACE;
        else if (closed_duration_ms < threshold_letter_word_)
            token = CW_LETTER_SPACE;
        else
            token = CW_WORD_SPACE;
    }
    update_unit();
    return token;
}

Token TimeClassifier::feed(int edge, float dt) {
    Token token = CW_NONE;
    if (edge != 0) {
        // The crossing sits dt before the start of this hop: the closing
        // interval ends there (giving dt back) and the new one opens with one
        // full hop plus dt. dt is 0 for a crossing at the hop boundary.
        // Skip closing the OFF interval when the end-of-message word space has
        // already been reported for it; it was accounted for below.
        if (is_now_on_ || !idle_reported_) {
            current_duration_ms_ -= dt;
            if (current_duration_ms_ < 0.0f)
                current_duration_ms_ = 0.0f;
            token = classify_and_update(is_now_on_, current_duration_ms_);
        }
        is_now_on_           = (edge > 0);
        current_duration_ms_ = static_cast<float>(BIN_SIZE_MS) + dt;
        if (edge > 0) {
            idle_reported_ = false;
        }
    } else {
        current_duration_ms_ += static_cast<float>(BIN_SIZE_MS);
    }

    // No following mark has closed this OFF stretch: report the end of the
    // message once, after the word gap, so the decoder emits the last character.
    if (!is_now_on_ && !idle_reported_) {
        const float limit = std::max(IDLE_WORD_K * threshold_elem_letter_, IDLE_WORD_MIN_MS);
        if (current_duration_ms_ >= limit) {
            idle_reported_       = true;
            current_duration_ms_ = 0.0f;
            token                = CW_WORD_SPACE;
        }
    }
    return token;
}

float TimeClassifier::get_current_wpm() const {
    return static_cast<float>(WPM_K) / unit_ms_;
}

bool TimeClassifier::is_signal_active() const {
    return is_now_on_;
}

} // namespace cw
