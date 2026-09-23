#pragma once

namespace cw {

// Bayesian power detector: turns the peak/noise power pair of one frame into a
// log-likelihood ratio (LLR) for the keying detector. The LLR is the frame SNR
// in dB relative to the configured threshold, so its zero crossing is the user
// threshold and its sign is the tone-present decision. Memoryless, so a single
// frame above threshold is enough to switch state.
class PowerDetector {
  public:
    void set_threshold_db(float db_val);

    float get_raw_llr(float peak_power, float noise_power);

  private:
    float snr_threshold_db_ = 10.0f; // Default 10 dB
};

} // namespace cw
