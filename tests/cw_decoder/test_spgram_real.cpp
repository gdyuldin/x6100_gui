#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

#include "cw_config.h"
#include "spgram_real.h"

TEST_CASE("spgram real: emits the first frame after FFT_SIZE and then every FFT_HOP") {
    cw::SpgramReal spgram;

    for (size_t i = 0; i + 1 < cw::FFT_SIZE; ++i) {
        REQUIRE_FALSE(spgram.execute(0.0f));
    }
    REQUIRE(spgram.execute(0.0f)); // FFT_SIZE-th sample fills the window

    for (size_t i = 0; i + 1 < cw::FFT_HOP; ++i) {
        REQUIRE_FALSE(spgram.execute(0.0f));
    }
    REQUIRE(spgram.execute(0.0f)); // one hop later
}

TEST_CASE("spgram real: the spectrum peaks at the tone bin") {
    cw::SpgramReal spgram;

    constexpr size_t TONE_BIN = 22;
    const double     dphi     = 2.0 * M_PI * static_cast<double>(TONE_BIN) / static_cast<double>(cw::FFT_SIZE);
    double           phase    = 0.0;
    for (size_t i = 0; i < cw::FFT_SIZE; ++i) {
        const bool ready = spgram.execute(static_cast<float>(std::cos(phase)));
        phase += dphi;
        if (i + 1 == cw::FFT_SIZE) {
            REQUIRE(ready);
        }
    }

    const cw::ComplexSpectrum &out    = spgram.get_fft_output();
    size_t                     peak   = 0;
    float                      max_p  = -1.0f;
    for (size_t i = 1; i < cw::SPECTRUM_SIZE; ++i) {
        const float p = std::norm(out[i]);
        if (p > max_p) {
            max_p = p;
            peak  = i;
        }
    }
    REQUIRE(peak == TONE_BIN);
}
