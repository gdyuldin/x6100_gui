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
// sample rate and a 128-point FFT) and inside the default 400-1200 Hz region.
constexpr size_t TONE_BIN   = 18;
// Bin 50 (1562.5 Hz) is above the default LPF edge.
constexpr size_t OUT_BIN    = 50;
constexpr float  NOISE_AMP  = 1.0f;
constexpr float  SIGNAL_AMP = 10.0f; // power ratio 100, well below the click guard

Frame make_frame(bool tone_on, size_t tone_bin = TONE_BIN) {
    Frame f;
    for (size_t i = 0; i < f.size(); ++i)
        f[i] = std::complex<float>(NOISE_AMP, 0.0f);
    if (tone_on)
        f[tone_bin] = std::complex<float>(SIGNAL_AMP, 0.0f);
    return f;
}

void feed(cw::CwReceiver &rx, bool tone_on, int frames, size_t tone_bin = TONE_BIN) {
    for (int i = 0; i < frames; ++i) {
        Frame f = make_frame(tone_on, tone_bin);
        rx.process_audio_frame(f.data());
    }
}

} // namespace

TEST_CASE("cw receiver: decodes a synthetic letter and reports WPM") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // "I" = dot dot: 6 ON, 6 OFF, 6 ON, then a 160 ms letter space closed by
    // one more ON frame.
    feed(rx, true, 6);
    feed(rx, false, 6);
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out == "I");
    REQUIRE(rx.get_measured_wpm() == Approx(25.0f).margin(3.0f));
}

TEST_CASE("cw receiver: change_threshold raises the decision point") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // At 60 dB even a strong tone is below the raised threshold and nothing is
    // decoded.
    rx.change_threshold(60.0f);

    // "E" = dot, then a 160 ms letter space.
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out.empty());
}


TEST_CASE("cw receiver: calling on_off") {
    bool           out;
    cw::CwReceiver rx([](const char *) {}, [&out](bool val){ out=val; });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // "E" = dot, then a 160 ms letter space.
    feed(rx, true, 6);
    REQUIRE(out == true);
    feed(rx, false, 16);
    REQUIRE(out == false);
    feed(rx, true, 1);
    REQUIRE(out == true);

}

TEST_CASE("cw receiver: get_tone_freq reports the detected tone frequency") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});

    feed(rx, true, 1);

    const float bin_hz = cw::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    REQUIRE(rx.get_tone_freq() == Approx(TONE_BIN * bin_hz).margin(bin_hz));
}

TEST_CASE("cw receiver: default region decodes a tone inside it") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});

    // TONE_BIN (562.5 Hz) is inside DEFAULT_HPF_HZ/DEFAULT_LPF_HZ and the
    // default threshold is low enough to detect it.
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out == "E");
}

TEST_CASE("cw receiver: a tone outside the default region is not decoded") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});

    // OUT_BIN (1562.5 Hz) is above DEFAULT_LPF_HZ, so the detector only sees
    // noise.
    feed(rx, true, 6, OUT_BIN);
    feed(rx, false, 16, OUT_BIN);
    feed(rx, true, 1, OUT_BIN);

    REQUIRE(out.empty());
}

TEST_CASE("cw receiver: changing the search region retargets detection") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool){});

    // The tone is outside the default region: nothing is decoded.
    feed(rx, true, 6, OUT_BIN);
    feed(rx, false, 16, OUT_BIN);
    feed(rx, true, 1, OUT_BIN);
    REQUIRE(out.empty());

    // Move the region around the tone; the same pattern now decodes.
    rx.change_hpf_hz(1400.0f);
    rx.change_lpf_hz(1800.0f);
    feed(rx, true, 6, OUT_BIN);
    feed(rx, false, 16, OUT_BIN);
    feed(rx, true, 1, OUT_BIN);

    REQUIRE(out == "E");
}
