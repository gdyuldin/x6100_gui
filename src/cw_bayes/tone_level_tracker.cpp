#include "tone_level_tracker.h"

#include <algorithm>
#include <cmath>

#include "cw_config.h"

namespace cw {

void ToneLevelTracker::reset() {
    level_     = 0.0f;
    ref_valid_ = false;
    was_on_    = false;
}

float ToneLevelTracker::norm_db(float power) const {
    if (!ref_valid_ || level_ <= 0.0f) {
        return INFINITY; // no reference: the relative condition is always satisfied
    }
    if (power <= 0.0f) {
        return -INFINITY;
    }
    return 10.0f * std::log10(power / level_);
}

void ToneLevelTracker::observe(float power, bool is_on) {
    if (is_on) {
        if (!was_on_) {
            level_     = power; // seed the reference from this mark
            ref_valid_ = true;
        }
        // Track the mark level with bounded per-frame steps: the slow release
        // follows a fade without chasing the key-up edge, the attack bound
        // rejects single-frame power spikes (clicks).
        const float upper = level_ * std::pow(10.0f, +LEVEL_ATTACK_DB / 10.0f);
        const float lower = level_ * std::pow(10.0f, -LEVEL_RELEASE_DB / 10.0f);
        level_            = std::min(std::max(power, lower), upper);
    }
    // Between marks the reference is deliberately kept: the sub-frame edge
    // interpolation needs the previous envelope, and re-seeding at the first ON
    // frame (was_on_ false -> level_ = power) keeps a weaker station after a
    // stronger one from being blocked. Acquisition is level-independent anyway.
    was_on_ = is_on;
}

} // namespace cw
