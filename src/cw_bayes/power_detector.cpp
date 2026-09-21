#include "power_detector.h"

#include <cmath>

namespace cw {

void PowerDetector::set_threshold_db(float db_val) {
    // +1.7 dB micro-correction compensating the Hann window ENBW
    snr_threshold_db_ = db_val + 1.7f;
}

void PowerDetector::reset() {
    llr_ = -3.0f;
}

float PowerDetector::get_raw_llr(float peak_power, float noise_power) {
    if (noise_power < 1e-10f)
        noise_power = 1e-10f;

    float instant_snr_lin = peak_power / noise_power;

    // Strong click guard (SNR > 50 dB): force deep silence immediately
    if (instant_snr_lin > 100000.0f) {
        llr_ = -3.99f;
        return llr_;
    }

    float instant_snr_db = 10.0f * std::log10(instant_snr_lin + 1e-5f);

    float current_llr = instant_snr_db - snr_threshold_db_;

    // Send with scaling to prevent single frame threshold changing
    return current_llr * 0.5f;
}

float PowerDetector::get_llr() const {
    return llr_;
}

} // namespace cw
