// Shared test helper for the external peak filter biquad. Used to validate the
// receiver's closed-form response (peak_filter_response.h) against a direct
// simulation of the difference equation and to colour synthesized audio with
// the real filter response in the receiver integration tests.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "cw_config.h"

namespace cwtest {

// CMSIS single-biquad form: y[n] = b0 x[n] + b2 x[n-2] + a1 y[n-1] + a2 y[n-2].
struct Biquad {
    float b0 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    static Biquad design(float key_tone_hz, float q, float fs) {
        q                = std::max(0.1f, q);
        const float w0   = 2.0f * static_cast<float>(M_PI) * key_tone_hz / fs;
        const float beta = std::sin(w0 / q * 0.5f);
        const float gain = 1.0f / (1.0f + beta);

        Biquad f;
        f.b0 = (1.0f - gain) * (1.0f + std::log10(q));
        f.b2 = -f.b0;
        f.a1 = 2.0f * gain * std::cos(w0);
        f.a2 = 1.0f - 2.0f * gain;
        return f;
    }

    float step(float x) {
        const float y = b0 * x + b2 * x2 + a1 * y1 + a2 * y2;
        x2            = x1;
        x1            = x;
        y2            = y1;
        y1            = y;
        return y;
    }
};

// |H(f)|^2 measured from the impulse response of the biquad designed at `fs`,
// independently of the closed form under test.
inline float measured_h2(float key_tone_hz, float q, float freq_hz, float fs) {
    Biquad             bq = Biquad::design(key_tone_hz, q, fs);
    constexpr size_t   N  = 8192;
    std::vector<float> h(N);
    for (size_t n = 0; n < N; ++n) {
        h[n] = bq.step(n == 0 ? 1.0f : 0.0f);
    }

    const double w  = 2.0 * M_PI * static_cast<double>(freq_hz) / static_cast<double>(fs);
    double       re = 0.0;
    double       im = 0.0;
    for (size_t n = 0; n < N; ++n) {
        re += h[n] * std::cos(w * static_cast<double>(n));
        im -= h[n] * std::sin(w * static_cast<double>(n));
    }
    return static_cast<float>(re * re + im * im);
}

} // namespace cwtest
