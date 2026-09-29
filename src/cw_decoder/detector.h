#pragma once

#include <cmath>

#include "cw_config.h"

namespace cw {

constexpr float db_to_lin(float db) {
    return powf(10.0f, db * 0.1f);
}

// One hop's decision.
// `edge` names the level crossing this hop confirmed:
//   +1 = OFF -> ON, -1 = ON -> OFF, 0 = held (no crossing in this hop).
// `dt` (ms) is the crossing offset for that edge: the time from the start of
// the reporting hop back to the interpolated crossing, so it is >= 0 and 0 is a
// valid crossing exactly at the hop boundary. It is meaningful only when
// `edge != 0`.
struct DetectorResult {
    int   edge;
    float dt;
};

class Detector {
    float t_on_ = 4.0f;
    float t_off_ = 2.0f;
    float t_cross_ = 2.0f;
    float snr_peak_ = 10.0f;
    bool state_ = false;
    size_t n_on_ = 2;
    size_t n_off_ = 2;
    size_t cnt_on_ = 0;
    size_t cnt_off_ = 0;

    // Absolute detection floor, set by the user (dB SNR). It only shifts the
    // absolute floors below; the relative margins stay fixed.
    float user_floor_db_ = 5.0f;

    // values for interpolation
    float r_before_ = 0.0f;
    float r_after_ = 0.0f;

    void update_thresholds(float r) {
        if (state_ == true && r > snr_peak_)
            snr_peak_ = r;
        float decay = (state_ == true) ? 0.97f : 0.94f;
        snr_peak_ *= decay;
        if (snr_peak_ < 1.0f) snr_peak_ = 1.0f;

        t_on_  = std::max(snr_peak_ * db_to_lin(-3.0f), db_to_lin(user_floor_db_));
        t_off_ = std::max(snr_peak_ * db_to_lin(-6.0f), db_to_lin(user_floor_db_ - 3.0f));

        t_cross_ = 1.0f + (snr_peak_ - 1.0f) * db_to_lin(-6.0f);
    }

public:
    DetectorResult feed(float power, float noise) {
        // A silent channel can leave the noise estimate at zero; keep the
        // decision OFF instead of producing an inf/NaN ratio.
        float r = (noise > 0.0f) ? power / noise : 0.0f;
        // Offset (samples) of the interpolated crossing behind the start of this
        // hop; >= 0 because interpolate_dt() <= 0.
        float crossing_offset = 0.0f;
        int edge = 0;
        bool pending_flip = false;
        if (state_ == false) {
            if (r > t_on_) {
                pending_flip = true;
                if (!cnt_on_) {
                    r_after_ = r;
                }
                cnt_on_++;
                if (cnt_on_ >= n_on_) {
                    crossing_offset = -interpolate_dt();
                    state_  = true;
                    cnt_on_ = 0;
                    edge    = +1;
                }
            } else {
                cnt_on_ = 0;
            }
        } else {
            if (r < t_off_) {
                pending_flip = true;
                if (!cnt_off_) {
                    r_after_ = r;
                }
                cnt_off_++;
                if (cnt_off_ >= n_off_) {
                    crossing_offset = -interpolate_dt();
                    state_   = false;
                    cnt_off_ = 0;
                    edge     = -1;
                }
            } else {
                cnt_off_ = 0;
            }
        }
        if (!pending_flip) {
            r_before_ = r;
        }
        update_thresholds(r);
        return {.edge = edge, .dt = 1000.0f * crossing_offset / SAMPLE_RATE};
    }

    float interpolate_dt() {
        float s_after = std::sqrt(std::max(r_after_ - 1.0f, 0.0f));
        float s_before = std::sqrt(std::max(r_before_ - 1.0f, 0.0f));
        float s_cross = std::sqrt(std::max(t_cross_ - 1.0f, 0.0f));
        float hop = static_cast<float>(COHERENT_INTEGRATOR_HOP);
        float size = static_cast<float>(COHERENT_INTEGRATOR_SIZE);

        float denom = s_after - s_before;
        if (std::abs(denom) < 1e-3f)
            return -hop; // small diff, cross close to prev hop

        float dt = -hop * (s_after - s_cross) / denom;

        if (dt > 0.0f)
            dt = 0.0f;
        else if (dt < -(size + hop))
            dt = -(size + hop);
        return dt;
    }

    void set_threshold(float db) {
        user_floor_db_ = db;
    }

    // Current Schmitt levels, as linear power/noise ratios (same unit as feed()).
    float on_threshold_lin() const { return t_on_; }
    float off_threshold_lin() const { return t_off_; }
};

} // namespace cw
