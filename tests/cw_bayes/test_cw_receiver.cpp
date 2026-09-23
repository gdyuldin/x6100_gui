// Integration tests for cw::CwReceiver. The receiver owns the audio->FFT stage
// and the coherent tone tracker, so the tests synthesize real time-domain 4 kHz
// audio (a keyed tone plus a broadband noise floor) and feed it end to end
// through CwReceiver::process_audio_frame.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "cw_config.h"
#include "cw_receiver.h"

using Catch::Approx;

namespace {

// Bin 22 sits inside the 300-900 Hz search region (bins 9-28 for a 4 kHz
// sample rate and a 128-point FFT) and inside the default 400-1200 Hz region,
// and it is close to the default 700 Hz tracker frequency so the coherent
// tracker is already near lock before the first frequency estimate arrives.
constexpr size_t TONE_BIN   = 22;
constexpr float  NOISE_AMP  = 1.0f;
constexpr float  SIGNAL_AMP = 10.0f; // power ratio 100

constexpr size_t HOP = cw::HOP_SIZE;

// The noise floor is a sum of one sinusoid per FFT bin with a deterministic
// phase, so its per-bin power equals the bin amplitude squared on the analyzer
// side, while the time-domain waveform is noise-like (not an impulse train).
std::array<double, cw::SPECTRUM_SIZE> bin_phase{};
double                                tone_phase = 0.0;

void reset_audio() {
    for (size_t i = 0; i < bin_phase.size(); ++i)
        bin_phase[i] = std::fmod(static_cast<double>(i) * 1.7 + 0.3, 2.0 * M_PI);
    tone_phase = 0.0;
}

// Elevated, non-uniform noise floor: bin i sits at NOISE_AMP + i * RAMP_STEP, so
// the 25% percentile differs from both the median and the minimum.
constexpr float NOISE_RAMP_STEP = 0.02f;
constexpr float RAMP_SIGNAL_AMP = 20.0f;

// Builds `hops` hops of contiguous real 4 kHz audio: the keyed tone at
// `tone_bin` (phase continuous across calls) plus the per-bin noise floor.
std::vector<float> make_audio(bool tone_on, size_t hops, bool ramp = false, size_t tone_bin = TONE_BIN,
                              float signal_amp = SIGNAL_AMP) {
    const size_t       n = hops * HOP;
    std::vector<float> out(n);

    const double                          tone_dphi = 2.0 * M_PI * static_cast<double>(tone_bin) / cw::FFT_SIZE;
    std::array<double, cw::SPECTRUM_SIZE> bin_dphi{};
    for (size_t i = 0; i < bin_dphi.size(); ++i)
        bin_dphi[i] = 2.0 * M_PI * static_cast<double>(i) / cw::FFT_SIZE;

    for (size_t s = 0; s < n; ++s) {
        double acc = 0.0;
        for (size_t i = 0; i < bin_dphi.size(); ++i) {
            const float amp = ramp ? (NOISE_AMP + static_cast<float>(i) * NOISE_RAMP_STEP) : NOISE_AMP;
            acc += static_cast<double>(amp) * std::cos(bin_phase[i]);
            bin_phase[i] += bin_dphi[i];
        }
        if (tone_on)
            acc += static_cast<double>(signal_amp) * std::cos(tone_phase);
        tone_phase += tone_dphi;
        out[s] = static_cast<float>(acc);
    }
    return out;
}

void feed(cw::CwReceiver &rx, bool tone_on, int frames, size_t tone_bin = TONE_BIN, float signal_amp = SIGNAL_AMP) {
    std::vector<float> seg = make_audio(tone_on, static_cast<size_t>(frames), false, tone_bin, signal_amp);
    rx.process_audio_frame(seg.size(), seg.data());
}

void feed_ramp(cw::CwReceiver &rx, bool tone_on, int frames, size_t tone_bin = TONE_BIN) {
    std::vector<float> seg = make_audio(tone_on, static_cast<size_t>(frames), true, tone_bin, RAMP_SIGNAL_AMP);
    rx.process_audio_frame(seg.size(), seg.data());
}

} // namespace

TEST_CASE("cw receiver: decodes a synthetic letter and reports WPM") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // "I" = dot dot: 6 ON, 6 OFF, 6 ON, then a 160 ms letter space closed by
    // the next mark. The level-relative mark/space timing keeps the measured
    // dot close to the nominal 6 frames (25 WPM).
    feed(rx, true, 6);
    feed(rx, false, 6);
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 6);

    REQUIRE(out == "I");
    // Sub-frame edge interpolation removed the whole-frame quantization, so the
    // reported speed now matches the nominal 25 WPM closely.
    REQUIRE(rx.get_measured_wpm() == Approx(25.0f).margin(3.0f));
}

TEST_CASE("cw receiver: dash-heavy 30 WPM does not collapse the reported speed") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // 30 WPM: dot = 40 ms (5 frames), dash = 120 ms; fed as 128 ms (16 frames) so
    // the dash peak sits just above the nominal dot search window, which used to
    // drag the dot/dash boundary upwards and collapse the reported speed to ~9.
    for (int rep = 0; rep < 25; ++rep) {
        feed(rx, true, 5);  // dot
        feed(rx, false, 5); // element space
        feed(rx, true, 16); // dash
        feed(rx, false, 5); // element space
    }

    REQUIRE(rx.get_measured_wpm() == Approx(30.0f).margin(2.0f));
}

TEST_CASE("cw receiver: reports 20 WPM") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // 20 WPM: dot = 60 ms. Fed as an 8-frame mark and a 7-frame space so the
    // mark/space pair averages back to the nominal unit.
    for (int rep = 0; rep < 30; ++rep) {
        feed(rx, true, 8);
        feed(rx, false, 7);
    }

    REQUIRE(rx.get_measured_wpm() == Approx(20.0f).margin(2.0f));
}

TEST_CASE("cw receiver: a weak station after a strong one is still decoded") {
    reset_audio();
    std::string    out;
    bool           prev_on  = false;
    int            on_edges = 0;
    cw::CwReceiver rx([&out](const char *text) { out += text; },
                      [&](bool on) {
                          if (on && !prev_on)
                              ++on_edges;
                          prev_on = on;
                      });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    constexpr float STRONG_AMP = 30.0f; // power ratio 900
    constexpr float WEAK_AMP   = 8.0f;  // -11.5 dB from STRONG_AMP

    // Strong station: 20 WPM, 8-frame marks / 7-frame spaces.
    for (int rep = 0; rep < 25; ++rep) {
        feed(rx, true, 8, TONE_BIN, STRONG_AMP);
        feed(rx, false, 7, TONE_BIN, STRONG_AMP);
    }
    const int strong_edges = on_edges;
    REQUIRE(strong_edges >= 20);

    // Weak station on the same tone, same 20 WPM. The kept reference is ~12 dB
    // above the weak marks, so this only works because acquisition is on the
    // absolute gate, not the relative level.
    for (int rep = 0; rep < 25; ++rep) {
        feed(rx, true, 8, TONE_BIN, WEAK_AMP);
        feed(rx, false, 7, TONE_BIN, WEAK_AMP);
    }
    const int weak_edges = on_edges - strong_edges;

    REQUIRE(weak_edges >= 20); // the weak marks were heard, not deafened
    REQUIRE(rx.get_measured_wpm() == Approx(20.0f).margin(2.0f));
}

// Regression: a single dot cluster at 12 WPM used to alias as a phantom dash at
// u/3 and latch the reported speed near 32 WPM.
TEST_CASE("cw receiver: a single dot cluster at 12 WPM does not alias") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // 12 WPM: dot = 100 ms ~= 13 frames (104 ms), element space likewise.
    for (int rep = 0; rep < 30; ++rep) {
        feed(rx, true, 13);
        feed(rx, false, 13);
    }

    REQUIRE(rx.get_measured_wpm() < 20.0f); // catches the u/3 latch
    REQUIRE(rx.get_measured_wpm() == Approx(12.0f).margin(2.0f));
}

TEST_CASE("cw receiver: change_threshold raises the decision point") {
    reset_audio();
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
    feed(rx, true, 6);

    REQUIRE(out.empty());
}

TEST_CASE("cw receiver: calling on_off") {
    reset_audio();
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
    feed(rx, true, 6);
    REQUIRE(out == true);
}

TEST_CASE("cw receiver: get_tone_freq reports the detected tone frequency") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    feed(rx, true, 6);

    const float bin_hz = cw::CwReceiver::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    REQUIRE(rx.get_tone_freq() == Approx(TONE_BIN * bin_hz).margin(bin_hz));
}

TEST_CASE("cw receiver: default region decodes a tone inside it") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    // TONE_BIN (562.5 Hz) is inside DEFAULT_HPF_HZ/DEFAULT_LPF_HZ and the
    // default threshold is low enough to detect it.
    feed(rx, true, 6);
    feed(rx, false, 16);
    feed(rx, true, 6);

    REQUIRE(out == "E");
}

// Note: no test feeds a tone outside the configured hpf|lpf. Those filters are
// applied to the audio upstream, so a tone outside the search region cannot
// reach the receiver.

TEST_CASE("cw receiver: decodes over a non-uniform elevated noise floor") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(12.0f);

    // The 25% percentile of the ramp is well below the tone power, so the
    // noise estimate keeps the detector open and "E" still decodes.
    feed_ramp(rx, true, 6);
    feed_ramp(rx, false, 16);
    feed_ramp(rx, true, 6);
    feed_ramp(rx, false, 8); // drain the trailing confirmation

    REQUIRE(out == "E");
}

TEST_CASE("cw receiver: on_frame fires once per processed FFT frame") {
    reset_audio();
    int            frames = 0;
    cw::CwReceiver rx([](const char *) {}, [](bool) {}, [&frames]() { ++frames; });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);

    // A block of k * HOP samples produces exactly k frames, even though the
    // last frames see a partially filled window.
    constexpr int      k   = 7;
    std::vector<float> seg = make_audio(true, k);
    rx.process_audio_frame(seg.size(), seg.data());

    REQUIRE(frames == k);
}
