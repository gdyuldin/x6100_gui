#include "power_detector.h"

#include <cmath>

namespace cw {

void PowerDetector::set_threshold_db(float db_val) {
    snr_threshold_db_ = db_val;
}

float PowerDetector::get_raw_llr(float peak_power, float noise_power) {
    if (noise_power < 1e-10f)
        noise_power = 1e-10f;

    float instant_snr_lin = peak_power / noise_power;
    float instant_snr_db  = 10.0f * std::log10(instant_snr_lin + 1e-5f);

    // Frame SNR in dB relative to the user threshold: zero crossing = threshold.
    return instant_snr_db - snr_threshold_db_;
}

} // namespace cw
