#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "cw_config.h"

namespace cw {

// The external CW peak filter is a two-pole resonator applied to the receive
// audio in the analog stage at this rate. Frequencies in Hz survive the
// resampling into the 4 kHz decoder stream, so the response is evaluated
// directly at the FFT bin frequencies.
constexpr float PEAK_FILTER_RATE = 12500.0f;

// |H|^2 floor: an out-of-region keyTone would otherwise divide by zero.
constexpr float PEAK_H2_FLOOR = 1e-8f;

// Magnitude response of the external peak filter, sampled per FFT bin. The
// receiver uses it to whiten the power spectrum before the noise estimate, so a
// filtered (coloured) noise floor still matches the white-noise calibration of
// NOISE_ALIGN. Disabled => the response is never computed and the receiver
// skips whitening entirely (h2 is left at 1.0).
class PeakFilterResponse {
    bool                             enabled_ = false;
    std::array<float, SPECTRUM_SIZE> h2_{};

  public:
    PeakFilterResponse() { h2_.fill(1.0f); }

    bool enabled() const { return enabled_; }

    const std::array<float, SPECTRUM_SIZE> &h2() const { return h2_; }

    // CMSIS biquad form: y[n] = b0 x[n] + b2 x[n-2] + a1 y[n-1] + a2 y[n-2], so
    // H(z) = b0 (1 - z^-2) / (1 - a1 z^-1 - a2 z^-2). Coefficients match the
    // hardware setup (b1 = 0, b2 = -b0).
    static float magnitude_sq_at(float key_tone_hz, float q, float freq_hz) {
        q                = std::max(0.1f, q);
        const float w0   = 2.0f * static_cast<float>(M_PI) * key_tone_hz / PEAK_FILTER_RATE;
        const float beta = std::sin(w0 / q * 0.5f);
        const float gain = 1.0f / (1.0f + beta);
        const float b0   = (1.0f - gain) * (1.0f + std::log10(q));
        const float a1   = 2.0f * gain * std::cos(w0);
        const float a2   = 1.0f - 2.0f * gain;

        const float w   = 2.0f * static_cast<float>(M_PI) * freq_hz / PEAK_FILTER_RATE;
        const float num = 2.0f * b0 * std::sin(w);
        const float dr  = 1.0f - a1 * std::cos(w) - a2 * std::cos(2.0f * w);
        const float di  = a1 * std::sin(w) + a2 * std::sin(2.0f * w);
        const float den = dr * dr + di * di;
        if (den <= 0.0f)
            return 0.0f;
        return num * num / den;
    }

    // Recomputes the per-bin response. When disabled the array is left as-is:
    // enabled() gates its use, so no work is done for the common filter-off case.
    void configure(bool on, float key_tone_hz, float q) {
        enabled_ = on;
        if (!on)
            return;

        const float bin_hz = SAMPLE_RATE / static_cast<float>(FFT_SIZE);
        for (size_t i = 0; i < SPECTRUM_SIZE; ++i) {
            const float h2 = magnitude_sq_at(key_tone_hz, q, static_cast<float>(i) * bin_hz);
            h2_[i]         = (h2 > PEAK_H2_FLOOR) ? h2 : PEAK_H2_FLOOR;
        }
    }
};

} // namespace cw
