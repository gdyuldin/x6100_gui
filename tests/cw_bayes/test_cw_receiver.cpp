// Integration tests for cw::CwReceiver over synthetic 65-bin spectra. Each
// frame carries a broadband noise floor in every bin; without it the median
// noise estimate would be ~0 and the detector's click guard would force every
// frame OFF.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <complex>
#include <string>

#include "cw_receiver.h"

using Catch::Approx;

namespace {

using Frame = std::array<std::complex<float>, cw::SPECTRUM_SIZE>;

// Bin 18 sits inside the 300-900 Hz search region (bins 9-28 for a 4 kHz
// sample rate and a 128-point FFT).
constexpr size_t TONE_BIN   = 18;
constexpr float  NOISE_AMP  = 1.0f;
constexpr float  SIGNAL_AMP = 10.0f; // power ratio 100, well below the click guard

Frame make_frame(bool tone_on) {
    Frame f;
    for (size_t i = 0; i < f.size(); ++i)
        f[i] = std::complex<float>(NOISE_AMP, 0.0f);
    if (tone_on)
        f[TONE_BIN] = std::complex<float>(SIGNAL_AMP, 0.0f);
    return f;
}

void feed(cw::CwReceiver &rx, bool tone_on, int frames) {
    for (int i = 0; i < frames; ++i) {
        Frame f = make_frame(tone_on);
        rx.process_audio_frame(f.data());
    }
}

} // namespace

TEST_CASE("cw receiver: decodes a synthetic letter and reports WPM") {
    std::string    out;
    cw::CwReceiver rx(300.0f, 900.0f, 12.0f, [&out](const char *text) { out += text; });

    // "I" = dot dot: 6 ON, 6 OFF, 6 ON, then a 160 ms letter space closed by
    // one more ON frame.
    feed(rx, true, 6);
    feed(rx, false, 6);
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out == "I");
    REQUIRE(rx.get_measured_wpm() == Approx(20.0f).margin(3.0f));
}

TEST_CASE("cw receiver: change_sensitivity raises the decision point") {
    std::string    out;
    cw::CwReceiver rx(300.0f, 900.0f, 12.0f, [&out](const char *text) { out += text; });

    // At 60 dB even a strong tone is below the raised threshold and nothing is
    // decoded.
    rx.change_sensitivity(60.0f);

    // "E" = dot, then a 160 ms letter space.
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out.empty());
}
