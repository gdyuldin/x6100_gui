#include "time_classifier.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cw {

namespace {


// Tuning of the classifier's adaptation, kept next to the code that uses it.
constexpr float ABS_ON_DB = 4.0f; // (S - th_user) above which a mark may start

// Asymmetric Schmitt levels for the mark envelope (dB below the tracked mark
// level). Acquisition is level-independent (absolute gate), so a stale
// reference cannot deafen the classifier to a weaker station; ON->OFF releases
// at NORM_RELEASE_DB, and a half-confirmed OFF transition is cancelled only by
// the shallower NORM_EDGE_ON_DB re-arm. The gap between the two is the
// hysteresis that stops an envelope dithering across one level from stretching
// a mark. Both are also the sub-frame interpolation levels for the matching
// edge, so a front edge is placed where the envelope crosses NORM_EDGE_ON_DB
// and a back edge where it crosses NORM_RELEASE_DB. The release level must stay
// above ToneLevelTracker's LEVEL_ATTACK_DB, so a bounded click cannot cut a
// mark.
constexpr float NORM_EDGE_ON_DB = 4.5f;
constexpr float NORM_RELEASE_DB = 9.0f;

// Minimum interval for a committed state (ms), used for both the young-state
// gate and the pending confirmation. Fixed in milliseconds, not derived from
// the tracked unit: it is the classifier's debounce, independent of speed.
constexpr float MIN_STATE_MS = 16.0f;

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

void TimeClassifier::add_sample(float duration_ms, float weight) {
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

    // Triangular kernel, normalised over the in-range bins so the added mass
    // stays `weight` and the wider kernel does not bias the centroid.
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
        hist_[bin] += weight * static_cast<float>(SPREAD_HALF_BINS + 1 - std::abs(k)) / norm;
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

void TimeClassifier::reset_speed() {
    hist_.fill(0.0f);
    hist_samples_      = 0;
    unit_ms_           = UNIT_MS_DEFAULT;
    candidate_unit_ms_ = -1.0f;
    relock_frames_     = 0;
    // The previous envelope belongs to the old signal; do not interpolate the
    // first post-reset edge across it, and drop any half-confirmed transition.
    // The committed state is dropped too, so a retune during a mark cannot emit
    // a mark measured across two different stations.
    prev_norm_valid_     = false;
    pending_active_      = false;
    off_idle_ms_         = 0.0f;
    idle_reported_       = false;
    is_now_on_           = false;
    current_duration_ms_ = 0.0f;
    accumulated_llr_     = 0.0f;
    frame_count_         = 0;
    update_boundaries();
}

// Runs the histogram update and token classification for one closed interval.
Token TimeClassifier::classify_and_update(bool closed_on, float closed_duration_ms, float closed_probability) {
    Token token;
    if (closed_on) {
        add_sample(closed_duration_ms, closed_probability);
        token = (closed_duration_ms < threshold_dot_dash_) ? CW_DOT : CW_DASH;
    } else {
        add_sample(closed_duration_ms, 1.0f - closed_probability);
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

// Emits CW_WORD_SPACE once an OFF stretch exceeds the idle limit.
Token TimeClassifier::handle_off_idle() {
    off_idle_ms_ += static_cast<float>(BIN_SIZE_MS);

    float limit = threshold_elem_letter_ * 5.0f;
    if (limit < 200.0f)
        limit = 200.0f;

    // Report the silence once per OFF stretch. Re-arming would emit a WORD_SPACE
    // every `limit` while the station stays silent, flooding the decoder and the
    // histogram with redundant boundaries.
    if (!idle_reported_ && off_idle_ms_ >= limit) {
        idle_reported_ = true;
        // Route the idle word space through the normal classifier so its
        // duration reaches the histogram and the unit stays consistent with the
        // emitted token, instead of returning a token that bypasses adaptation.
        const Token token = classify_and_update(false, off_idle_ms_, 0.0f);
        off_idle_ms_      = 0.0f;
        return token;
    }
    return CW_NONE;
}

Token TimeClassifier::feed_frame(float abs_llr, float norm_db, bool ref_valid) {
    // ToneLevelTracker::norm_db() returns +/-INFINITY as "no reference"/"no
    // power" sentinels; anything finite is a real level.
    const bool  norm_finite = !std::isinf(norm_db);
    const float prev_norm   = prev_norm_db_;
    const bool  prev_valid  = prev_norm_valid_;
    prev_norm_db_           = norm_db;
    prev_norm_valid_        = norm_finite;

    // Asymmetric Schmitt on the tracked level. Acquisition is level-independent
    // (absolute gate only): a stale reference left over a gap must not deafen
    // the classifier to a weaker station. Release and re-arm are relative, and
    // the pending override below keeps their hysteresis sticky.
    bool want = is_now_on_;
    if (!is_now_on_) {
        if (abs_llr > ABS_ON_DB)
            want = true;
    } else {
        if (!ref_valid || !norm_finite || norm_db < -NORM_RELEASE_DB)
            want = false;
    }
    if (pending_active_) {
        // Do not cancel a transition on a mere re-cross of the level it just
        // left: require the opposite crossing (NORM_EDGE_ON_DB to come back ON,
        // the absolute gate to stay OFF). A closing mark also needs a live
        // reference, since its release decision is relative.
        if (!pending_level_)
            want = (abs_llr > ABS_ON_DB) && ref_valid && norm_finite && (norm_db > -NORM_EDGE_ON_DB);
        else
            // Acquisition is level-independent: hold the ON pending while the
            // absolute gate is up, so a weaker station after a stronger one
            // (stale reference) is still heard.
            want = (abs_llr > ABS_ON_DB);
    }
    if (want) {
        off_idle_ms_   = 0.0f;
        idle_reported_ = false;
    }

    // Minimum interval for a committed state; used for both the young-state
    // gate and the pending confirmation.
    const float min_state_ms = MIN_STATE_MS;

    if (pending_active_) {
        if (want == pending_level_) {
            pending_duration_ms_ += static_cast<float>(BIN_SIZE_MS);
            pending_llr_ += abs_llr;
            pending_count_++;
            if (pending_duration_ms_ >= min_state_ms) {
                const Token token    = classify_and_update(is_now_on_, closed_duration_ms_, closed_probability_);
                is_now_on_           = pending_level_;
                current_duration_ms_ = pending_duration_ms_;
                accumulated_llr_     = pending_llr_;
                frame_count_         = pending_count_;
                pending_active_      = false;
                return token;
            }
            return CW_NONE;
        }
        // Reverted before confirmation: merge the excursion back. Interpolate
        // the re-arm crossing so the merged frame is not snapped to the
        // committed state, which would bias the interval by up to a frame.
        float rearm_frac = 0.0f;
        if (prev_valid && norm_finite) {
            const float denom    = norm_db - prev_norm;
            const float rearm_db = is_now_on_ ? -NORM_EDGE_ON_DB : -NORM_RELEASE_DB;
            if (std::fabs(denom) > 1e-3f)
                rearm_frac = std::clamp((rearm_db - prev_norm) / denom, 0.0f, 1.0f);
        }
        pending_active_ = false;
        current_duration_ms_ =
            closed_duration_ms_ + pending_duration_ms_ + (1.0f - rearm_frac) * static_cast<float>(BIN_SIZE_MS);
        accumulated_llr_ += abs_llr;
        frame_count_++;
        return is_now_on_ ? CW_NONE : handle_off_idle();
    }

    if (want == is_now_on_) {
        current_duration_ms_ += static_cast<float>(BIN_SIZE_MS);
        accumulated_llr_ += abs_llr;
        frame_count_++;
        return is_now_on_ ? CW_NONE : handle_off_idle();
    }

    // Candidate transition: interpolate the crossing inside the current frame
    // at the level that drives that direction of the Schmitt.
    const float edge_level_db = want ? -NORM_EDGE_ON_DB : -NORM_RELEASE_DB;
    float       edge_frac     = 0.0f;
    if (prev_valid && norm_finite) {
        const float denom = norm_db - prev_norm;
        if (std::fabs(denom) > 1e-3f)
            edge_frac = std::clamp((edge_level_db - prev_norm) / denom, 0.0f, 1.0f);
    }
    const float corrected_ms = current_duration_ms_ + edge_frac * static_cast<float>(BIN_SIZE_MS);

    if (corrected_ms < min_state_ms) {
        // Leaving a young committed state: a click, absorb the frame.
        current_duration_ms_ += static_cast<float>(BIN_SIZE_MS);
        accumulated_llr_ += abs_llr;
        frame_count_++;
        return is_now_on_ ? CW_NONE : handle_off_idle();
    }

    // Start the deferred transition; the closed interval waits for confirmation.
    pending_active_      = true;
    pending_level_       = want;
    pending_duration_ms_ = (1.0f - edge_frac) * static_cast<float>(BIN_SIZE_MS);
    pending_llr_         = abs_llr;
    pending_count_       = 1;
    closed_duration_ms_  = corrected_ms;
    closed_probability_  = 1.0f / (1.0f + std::exp(-(accumulated_llr_ / static_cast<float>(frame_count_))));
    return CW_NONE;
}

float TimeClassifier::get_current_wpm() const {
    return static_cast<float>(WPM_K) / unit_ms_;
}

bool TimeClassifier::is_signal_active() const {
    return is_now_on_;
}

} // namespace cw
