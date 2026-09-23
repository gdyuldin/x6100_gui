// Integration tests for cw::CwReceiver. The receiver owns the audio->FFT stage,
// so the tests synthesize time-domain 4 kHz audio from an intended 65-bin
// spectrum (inverse FFT + Hann overlap-add) and feed it as real samples end to
// end through CwReceiver::process_audio_frame.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <complex>
#include <string>
#include <vector>

#include <liquid/liquid.h>

#include "cw_config.h"
#include "cw_receiver.h"

using Catch::Approx;

namespace {

using Frame = cw::ComplexSpectrum;

// Bin 18 sits inside the 300-900 Hz search region (bins 9-28 for a 4 kHz
// sample rate and a 128-point FFT) and inside the default 400-1200 Hz region.
constexpr size_t TONE_BIN = 18;
// Bin 50 (1562.5 Hz) is above the default LPF edge.
constexpr size_t OUT_BIN    = 50;
constexpr float  NOISE_AMP  = 1.0f;
constexpr float  SIGNAL_AMP = 10.0f; // power ratio 100, well below the click guard

constexpr size_t HOP = cw::HOP_SIZE;

Frame make_frame(bool tone_on, size_t tone_bin = TONE_BIN) {
    Frame f;
    for (size_t i = 0; i < f.size(); ++i)
        f[i] = std::complex<float>(NOISE_AMP, 0.0f);
    if (tone_on)
        f[tone_bin] = std::complex<float>(SIGNAL_AMP, 0.0f);
    return f;
}

// Elevated, non-uniform noise floor: bin i sits at NOISE_AMP + i * RAMP_STEP, so
// the 25% percentile differs from both the median and the minimum. The tone is
// stronger than SIGNAL_AMP so that, against the percentile floor (higher than
// the old minimum-based estimate), the frame LLR still clears the +3.0 Schmitt
// trigger.
constexpr float NOISE_RAMP_STEP = 0.02f;
constexpr float RAMP_SIGNAL_AMP = 20.0f;

Frame make_ramp_frame(bool tone_on, size_t tone_bin = TONE_BIN) {
    Frame f;
    for (size_t i = 0; i < f.size(); ++i)
        f[i] = std::complex<float>(NOISE_AMP + static_cast<float>(i) * NOISE_RAMP_STEP, 0.0f);
    if (tone_on)
        f[tone_bin] = std::complex<float>(RAMP_SIGNAL_AMP, 0.0f);
    return f;
}

// Builds `hops` hops of contiguous real audio whose sliding Hann-windowed FFT
// approximates `frame`. The full FFT_SIZE Hermitian spectrum is reconstructed
// (DC and Nyquist real), transformed with an inverse FFT, multiplied by the same
// Hann window and overlap-added at hop = FFT_SIZE/4 (Hann^2 satisfies COLA at
// that hop).
std::vector<float> synthesize(const Frame &frame, size_t hops) {
    std::array<std::complex<float>, cw::FFT_SIZE> spectrum{};
    std::array<std::complex<float>, cw::FFT_SIZE> time{};

    spectrum[0]                = std::complex<float>(frame[0].real(), 0.0f);
    spectrum[cw::FFT_SIZE / 2] = std::complex<float>(frame[cw::FFT_SIZE / 2].real(), 0.0f);
    for (size_t i = 1; i < cw::FFT_SIZE / 2; ++i) {
        spectrum[i]                = frame[i];
        spectrum[cw::FFT_SIZE - i] = std::conj(frame[i]);
    }

    fft_run(cw::FFT_SIZE, spectrum.data(), time.data(), LIQUID_FFT_BACKWARD, 0);

    std::array<float, cw::FFT_SIZE> window{};
    for (size_t i = 0; i < cw::FFT_SIZE; ++i)
        window[i] = liquid_hann(i, cw::FFT_SIZE);

    std::vector<float> out(hops * HOP, 0.0f);
    for (size_t hop = 0; hop < hops; ++hop) {
        for (size_t i = 0; i < cw::FFT_SIZE; ++i) {
            size_t idx = hop * HOP + i;
            if (idx < out.size())
                out[idx] += time[i].real() * window[i];
        }
    }
    return out;
}

void feed(cw::CwReceiver &rx, bool tone_on, int frames, size_t tone_bin = TONE_BIN) {
    std::vector<float> seg = synthesize(make_frame(tone_on, tone_bin), static_cast<size_t>(frames));
    rx.process_audio_frame(seg.size(), seg.data());
}

void feed_ramp(cw::CwReceiver &rx, bool tone_on, int frames, size_t tone_bin = TONE_BIN) {
    std::vector<float> seg = synthesize(make_ramp_frame(tone_on, tone_bin), static_cast<size_t>(frames));
    rx.process_audio_frame(seg.size(), seg.data());
}

} // namespace

TEST_CASE("cw receiver: decodes a synthetic letter and reports WPM") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
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
    // The 6-frame dot (48 ms) is only 1.5x the 32 ms Hann analysis window, so
    // the first/last ON frames lose SNR at the edges and the measured keying is
    // shorter than the nominal 25 WPM (this is a real property of the streaming
    // FFT, not of the decoder).
    REQUIRE(rx.get_measured_wpm() == Approx(34.5f).margin(3.0f));
}

TEST_CASE("cw receiver: change_threshold raises the decision point") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
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
    cw::CwReceiver rx([](const char *) {}, [&out](bool val) { out = val; });
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
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    feed(rx, true, 1);

    const float bin_hz = cw::CwReceiver::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    REQUIRE(rx.get_tone_freq() == Approx(TONE_BIN * bin_hz).margin(bin_hz));
}

TEST_CASE("cw receiver: default region decodes a tone inside it") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    // TONE_BIN (562.5 Hz) is inside DEFAULT_HPF_HZ/DEFAULT_LPF_HZ and the
    // default threshold is low enough to detect it.
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 1);

    REQUIRE(out == "E");
}

TEST_CASE("cw receiver: a tone outside the default region is not decoded") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    // OUT_BIN (1562.5 Hz) is above DEFAULT_LPF_HZ, so the detector only sees
    // noise.
    feed(rx, true, 6, OUT_BIN);
    feed(rx, false, 16, OUT_BIN);
    feed(rx, true, 1, OUT_BIN);

    REQUIRE(out.empty());
}

TEST_CASE("cw receiver: changing the search region retargets detection") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

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

TEST_CASE("cw receiver: decodes over a non-uniform elevated noise floor") {
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // The 25% percentile of the ramp is well below the tone power, so the
    // seeded EMA estimate keeps the detector open; "E" still decodes and the
    // click guard is not triggered.
    feed_ramp(rx, true, 6);
    feed_ramp(rx, false, 16);
    feed_ramp(rx, true, 1);

    REQUIRE(out == "E");
}

TEST_CASE("cw receiver: on_frame fires once per processed FFT frame") {
    int            frames = 0;
    cw::CwReceiver rx([](const char *) {}, [](bool) {}, [&frames]() { ++frames; });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);

    // A block of k * HOP samples produces exactly k frames, even though the
    // last frames see a partially filled window.
    constexpr int      k   = 7;
    std::vector<float> seg = synthesize(make_frame(true), k);
    rx.process_audio_frame(seg.size(), seg.data());

    REQUIRE(frames == k);
}
