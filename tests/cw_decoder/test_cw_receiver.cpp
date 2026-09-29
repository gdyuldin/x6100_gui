// Integration tests for cw::CwReceiver. The receiver owns the audio->FFT stage
// and the coherent tone tracker, so the tests synthesize real time-domain 4 kHz
// audio (a keyed tone plus a broadband noise floor) and feed it end to end
// through CwReceiver::process_audio_frame.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "cw_config.h"
#include "cw_receiver.h"
#include "cw_receiver_diag_access.h"
#include "peak_biquad_test.h"
#include "peak_filter_response.h"

using Catch::Approx;

namespace {

// Bin 45 is ~703 Hz, close to the tracker's 700 Hz default NCO, so the tone is
// already inside the baseband filter before any retune (the WPM tests below
// only exercise the detector/classifier, not the tracking policy).
constexpr size_t TONE_BIN   = 45;
constexpr float  NOISE_AMP  = 1.0f;
constexpr float  SIGNAL_AMP = 10.0f; // power ratio 100

// A second, retune-target bin (bin 22 = 562.5 Hz), ~140 Hz away from the NCO.
constexpr size_t ALT_TONE_BIN = 22;

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

// Builds `n` samples of contiguous real 4 kHz audio: the keyed tone at
// `tone_bin` (phase continuous across calls) plus the per-bin noise floor.
// `gain` (optional) scales every bin, emulating a linear filter's magnitude
// response in the frequency domain.
std::vector<float> make_audio(bool tone_on, size_t n, bool ramp = false, size_t tone_bin = TONE_BIN,
                              float                                       signal_amp = SIGNAL_AMP,
                              const std::array<float, cw::SPECTRUM_SIZE> *gain       = nullptr) {
    std::vector<float> out(n);

    const double                          tone_dphi = 2.0 * M_PI * static_cast<double>(tone_bin) / cw::FFT_SIZE;
    std::array<double, cw::SPECTRUM_SIZE> bin_dphi{};
    for (size_t i = 0; i < bin_dphi.size(); ++i)
        bin_dphi[i] = 2.0 * M_PI * static_cast<double>(i) / cw::FFT_SIZE;

    const float tone_gain = gain ? (*gain)[tone_bin] : 1.0f;

    for (size_t s = 0; s < n; ++s) {
        double acc = 0.0;
        for (size_t i = 0; i < bin_dphi.size(); ++i) {
            const float base = ramp ? (NOISE_AMP + static_cast<float>(i) * NOISE_RAMP_STEP) : NOISE_AMP;
            const float amp  = gain ? base * (*gain)[i] : base;
            acc += static_cast<double>(amp) * std::cos(bin_phase[i]);
            bin_phase[i] += bin_dphi[i];
        }
        if (tone_on)
            acc += static_cast<double>(signal_amp * tone_gain) * std::cos(tone_phase);
        tone_phase += tone_dphi;
        out[s] = static_cast<float>(acc);
    }
    return out;
}

// Feeds `ms` milliseconds of keyed tone (or silence) to the receiver.
void feed_ms(cw::CwReceiver &rx, bool tone_on, float ms, size_t tone_bin = TONE_BIN, float signal_amp = SIGNAL_AMP) {
    const size_t       n   = static_cast<size_t>(std::lround(ms * cw::SAMPLE_RATE / 1000.0f));
    std::vector<float> seg = make_audio(tone_on, n, false, tone_bin, signal_amp);
    rx.process_audio_frame(seg.size(), seg.data());
}

void feed_ramp(cw::CwReceiver &rx, bool tone_on, float ms, size_t tone_bin = TONE_BIN) {
    const size_t       n   = static_cast<size_t>(std::lround(ms * cw::SAMPLE_RATE / 1000.0f));
    std::vector<float> seg = make_audio(tone_on, n, true, tone_bin, RAMP_SIGNAL_AMP);
    rx.process_audio_frame(seg.size(), seg.data());
}

// Per-bin magnitude response of the external analog peak filter (designed at the
// hardware rate, as the analog stage does), measured from the biquad impulse
// response so the receiver's closed form is checked against the recurrence.
std::array<float, cw::SPECTRUM_SIZE> peak_filter_gain(float key_hz, float q) {
    std::array<float, cw::SPECTRUM_SIZE> gain{};
    const float                          bin_hz = cw::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    for (size_t i = 0; i < cw::SPECTRUM_SIZE; ++i)
        gain[i] = std::sqrt(cwtest::measured_h2(key_hz, q, static_cast<float>(i) * bin_hz, cw::PEAK_FILTER_RATE));
    return gain;
}

const float PEAK_KEY_HZ = TONE_BIN * cw::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);

// Feeds `ms` milliseconds of audio coloured by the peak filter's magnitude
// response (frequency-domain shaping: no resampling/ringing artifacts).
void feed_shaped(cw::CwReceiver &rx, const std::array<float, cw::SPECTRUM_SIZE> &gain, bool tone_on, float ms,
                 size_t tone_bin = TONE_BIN, float signal_amp = SIGNAL_AMP) {
    const size_t       n   = static_cast<size_t>(std::lround(ms * cw::SAMPLE_RATE / 1000.0f));
    std::vector<float> seg = make_audio(tone_on, n, false, tone_bin, signal_amp, &gain);
    rx.process_audio_frame(seg.size(), seg.data());
}

// Locks the tone tracker to the test tone and settles the detector before the
// real sequence, without leaving a pending character behind: a long mark forces
// the NCO retune, and the following silence flushes (and discards) whatever it
// decoded. The caller clears its captured text after priming.
void prime(cw::CwReceiver &rx, bool ramp = false, size_t tone_bin = TONE_BIN) {
    if (ramp) {
        feed_ramp(rx, true, 320, tone_bin);
        feed_ramp(rx, false, 900, tone_bin);
    } else {
        feed_ms(rx, true, 320, tone_bin);
        feed_ms(rx, false, 900, tone_bin);
    }
}

} // namespace

TEST_CASE("cw receiver: decodes a synthetic letter and reports WPM") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // "I" = dot dot: 48 ms marks at 25 WPM, element spaces 48 ms, then a 160 ms
    // letter space closed by the next mark.
    prime(rx);
    out.clear();
    feed_ms(rx, true, 48);
    feed_ms(rx, false, 48);
    feed_ms(rx, true, 48);
    feed_ms(rx, false, 160);
    feed_ms(rx, true, 48);

    REQUIRE(out == "I");
    // The short sequence seeds the speed from its first intervals; the dedicated
    // speed tests below cover the WPM estimate.
    REQUIRE(rx.get_measured_wpm() > 15.0f);
}

TEST_CASE("cw receiver: dash-heavy 30 WPM does not collapse the reported speed") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // 30 WPM: dot 40 ms, dash 128 ms (a touch above the nominal 120 ms).
    for (int rep = 0; rep < 25; ++rep) {
        feed_ms(rx, true, 40);
        feed_ms(rx, false, 40);
        feed_ms(rx, true, 128);
        feed_ms(rx, false, 40);
    }

    // The split crossing keeps the measured intervals at the keyed length; the
    // point is that the speed does not collapse.
    REQUIRE(rx.get_measured_wpm() > 24.0f);
    REQUIRE(rx.get_measured_wpm() == Approx(30.0f).margin(4.0f));
}

TEST_CASE("cw receiver: reports 20 WPM") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // 20 WPM: dot = 60 ms, keyed symmetrically now that the crossing is split.
    for (int rep = 0; rep < 30; ++rep) {
        feed_ms(rx, true, 60);
        feed_ms(rx, false, 60);
    }

    REQUIRE(rx.get_measured_wpm() == Approx(20.0f).margin(3.0f));
}

// Regression: the transition hop used to be credited whole to the new interval,
// inflating every element by the interpolation offset; a 25 WPM keyer read as
// ~22 WPM. The split must recover the keyed speed.
TEST_CASE("cw receiver: an exact 25 WPM stream reports 25 WPM") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // 25 WPM: dot = element space = 48 ms.
    for (int rep = 0; rep < 30; ++rep) {
        feed_ms(rx, true, 48);
        feed_ms(rx, false, 48);
    }

    REQUIRE(rx.get_measured_wpm() == Approx(25.0f).margin(1.5f));
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
    rx.change_threshold(5.0f);

    constexpr float STRONG_AMP = 30.0f; // power ratio 900
    constexpr float WEAK_AMP   = 8.0f;  // -11.5 dB from STRONG_AMP

    // Strong station: 20 WPM, symmetric 60 ms marks / spaces.
    for (int rep = 0; rep < 25; ++rep) {
        feed_ms(rx, true, 60, TONE_BIN, STRONG_AMP);
        feed_ms(rx, false, 60, TONE_BIN, STRONG_AMP);
    }
    const int strong_edges = on_edges;
    REQUIRE(strong_edges >= 20);

    // Weak station on the same tone, same 20 WPM. The kept peak reference is
    // ~12 dB above the weak marks, so acquisition relies on the absolute floor
    // and on the peak reference decaying while the detector is OFF.
    for (int rep = 0; rep < 25; ++rep) {
        feed_ms(rx, true, 60, TONE_BIN, WEAK_AMP);
        feed_ms(rx, false, 60, TONE_BIN, WEAK_AMP);
    }
    const int weak_edges = on_edges - strong_edges;

    REQUIRE(weak_edges >= 10); // the weak marks were heard, not deafened
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
    rx.change_threshold(5.0f);

    // 12 WPM: dot = 100 ms ~= 104 ms, element space likewise.
    for (int rep = 0; rep < 30; ++rep) {
        feed_ms(rx, true, 104);
        feed_ms(rx, false, 104);
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
    rx.change_threshold(5.0f);

    // Far above any achievable SNR: nothing opens.
    rx.change_threshold(60.0f);

    feed_ms(rx, true, 48);
    feed_ms(rx, false, 160);
    feed_ms(rx, true, 48);

    REQUIRE(out.empty());
}

TEST_CASE("cw receiver: calling on_off") {
    reset_audio();
    bool           out = false;
    cw::CwReceiver rx([](const char *) {}, [&out](bool val) { out = val; });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    prime(rx);
    REQUIRE(out == false);

    feed_ms(rx, true, 48);
    REQUIRE(out == true);
    feed_ms(rx, false, 160);
    REQUIRE(out == false);
    feed_ms(rx, true, 48);
    REQUIRE(out == true);
}

TEST_CASE("cw receiver: get_tone_freq reports the detected tone frequency") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    feed_ms(rx, true, 64);

    const float bin_hz = cw::CwReceiver::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    REQUIRE(rx.get_tone_freq() == Approx(TONE_BIN * bin_hz).margin(2.0f * bin_hz));
}

TEST_CASE("cw receiver: default region decodes a tone inside it") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});

    // TONE_BIN (703 Hz) is inside DEFAULT_HPF_HZ/DEFAULT_LPF_HZ and the default
    // threshold is low enough to detect it.
    prime(rx);
    out.clear();

    feed_ms(rx, true, 48);
    feed_ms(rx, false, 160);
    feed_ms(rx, true, 48);

    REQUIRE(out == "E");
}

// A transmission that ends with no following mark must still reach the user:
// after the word gap the classifier reports one word space, which flushes the
// pending character.
TEST_CASE("cw receiver: the last character is flushed at the end of a message") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    prime(rx);
    out.clear();
    feed_ms(rx, true, 48);   // "E"
    feed_ms(rx, false, 900); // silence with no next mark

    REQUIRE(out == "E "); // the dot, then the flushed word space
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
    rx.change_threshold(5.0f);

    // The 25% percentile of the ramp is well below the tone power, so the
    // noise estimate keeps the detector open and "E" still decodes.
    prime(rx, /*ramp=*/true);
    out.clear();

    feed_ramp(rx, true, 48);
    feed_ramp(rx, false, 160);
    feed_ramp(rx, true, 48);
    feed_ramp(rx, false, 64); // drain the trailing confirmation

    REQUIRE(out == "E");
}

// The NCO starts at 700 Hz; a tone ~140 Hz away must be acquired after the
// tracker has been quiet long enough (the veto only delays a retune while the
// current tone is alive).
TEST_CASE("cw receiver: retunes to a station after silence") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // Quiet long enough to expire the veto, then a station on the alt bin.
    feed_ms(rx, false, 1100);
    feed_ms(rx, true, 1600, ALT_TONE_BIN);

    const float bin_hz = cw::CwReceiver::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    REQUIRE(cw::CwReceiverDiagAccess::nco_hz(rx) == Approx(ALT_TONE_BIN * bin_hz).margin(2.0f * bin_hz));
}

// While the tracked tone is alive a competing peak must not pull the NCO away.
TEST_CASE("cw receiver: the veto holds the NCO during a brief dip") {
    reset_audio();
    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    // Lock onto the alt bin (retune is allowed because the veto starts expired).
    feed_ms(rx, true, 400, ALT_TONE_BIN);
    const float locked_hz = cw::CwReceiverDiagAccess::nco_hz(rx);

    // A short gap (well under the 1 s veto) must not move the NCO.
    feed_ms(rx, false, 400);
    REQUIRE(cw::CwReceiverDiagAccess::nco_hz(rx) == Approx(locked_hz).margin(2.0f));

    (void)out;
}

TEST_CASE("cw receiver: on_frame fires once per coherent integrator hop") {
    reset_audio();
    int            frames = 0;
    cw::CwReceiver rx([](const char *) {}, [](bool) {}, [&frames]() { ++frames; });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);

    // A block of k FFT hops produces frames only after the first FFT window is
    // full, then one per integrator hop. The tone sits at the default NCO
    // frequency so no retune restarts the hop mid-block.
    constexpr size_t   NCO_BIN = 43; // ~672 Hz, next to the tracker's 670 Hz
    constexpr int      k       = 7;
    std::vector<float> seg     = make_audio(true, k * cw::FFT_HOP, false, NCO_BIN);
    rx.process_audio_frame(seg.size(), seg.data());

    const int expected = static_cast<int>((seg.size() - cw::FFT_SIZE) / cw::COHERENT_INTEGRATOR_HOP) + 1;
    REQUIRE(frames == expected);
}

// Emulates a receive chain with the external peak filter in front of the
// decoder: the synthesized audio is coloured by the filter magnitude response
// and the receiver is told about the filter through change_peak_filter(). The
// whitening should keep the noise estimate (and thus the SNR) filter-independent.

TEST_CASE("cw receiver: decodes a peak-filtered keyed tone") {
    reset_audio();
    const float q    = 10.0f;
    const auto  gain = peak_filter_gain(PEAK_KEY_HZ, q);

    std::string    out;
    cw::CwReceiver rx([&out](const char *text) { out += text; }, [](bool) {});
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);
    rx.change_peak_filter(true, PEAK_KEY_HZ, q);

    // Lock the tracker and flush whatever the priming decoded.
    feed_shaped(rx, gain, true, 320.0f);
    feed_shaped(rx, gain, false, 900.0f);
    out.clear();

    feed_shaped(rx, gain, true, 48.0f);
    feed_shaped(rx, gain, false, 160.0f);
    feed_shaped(rx, gain, true, 48.0f);

    REQUIRE(out == "E");
}

TEST_CASE("cw receiver: peak-filtered noise does not open the detector") {
    reset_audio();
    const float q    = 10.0f;
    const auto  gain = peak_filter_gain(PEAK_KEY_HZ, q);

    std::string    out;
    int            on_edges = 0;
    bool           prev_on  = false;
    cw::CwReceiver rx([&out](const char *text) { out += text; },
                      [&](bool on) {
                          if (on && !prev_on)
                              ++on_edges;
                          prev_on = on;
                      });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);
    rx.change_peak_filter(true, PEAK_KEY_HZ, q);

    feed_shaped(rx, gain, false, 1000.0f);

    REQUIRE(on_edges == 0);
    // The idle handler may flush one word space, but no character may decode.
    REQUIRE(out.find_first_not_of(' ') == std::string::npos);
}

TEST_CASE("cw receiver: ignoring the peak filter inflates the noise estimate") {
    reset_audio();
    const float q    = 10.0f;
    const auto  gain = peak_filter_gain(PEAK_KEY_HZ, q);

    const size_t       n     = static_cast<size_t>(std::lround(800.0f * cw::SAMPLE_RATE / 1000.0f));
    std::vector<float> noise = make_audio(false, n, false, TONE_BIN, SIGNAL_AMP, &gain);

    cw::CwReceiver aware([](const char *) {}, [](bool) {});
    aware.change_hpf_hz(300.0f);
    aware.change_lpf_hz(900.0f);
    aware.change_threshold(5.0f);
    aware.change_peak_filter(true, PEAK_KEY_HZ, q);

    cw::CwReceiver unaware([](const char *) {}, [](bool) {});
    unaware.change_hpf_hz(300.0f);
    unaware.change_lpf_hz(900.0f);
    unaware.change_threshold(5.0f);

    // Feed the same coloured noise to both, sampling the per-frame SNR.
    float        aware_snr   = 0.0f;
    float        unaware_snr = 0.0f;
    const size_t chunk       = n / 8;
    for (size_t k = 0; k < 8; ++k) {
        aware.process_audio_frame(chunk, noise.data() + k * chunk);
        unaware.process_audio_frame(chunk, noise.data() + k * chunk);
        aware_snr   = std::max(aware_snr, cw::CwReceiverDiagAccess::snr_lin(aware));
        unaware_snr = std::max(unaware_snr, cw::CwReceiverDiagAccess::snr_lin(unaware));
    }

    INFO("aware=" << aware_snr << " unaware=" << unaware_snr);
    REQUIRE(unaware_snr > 4.0f * aware_snr);
}

TEST_CASE("cw receiver: change_peak_filter is safe to toggle live") {
    reset_audio();
    const auto gain = peak_filter_gain(PEAK_KEY_HZ, 10.0f);

    int            on_edges = 0;
    bool           prev_on  = false;
    cw::CwReceiver rx([](const char *) {},
                      [&](bool on) {
                          if (on && !prev_on)
                              ++on_edges;
                          prev_on = on;
                      });
    rx.change_hpf_hz(300.0f);
    rx.change_lpf_hz(900.0f);
    rx.change_threshold(5.0f);

    feed_shaped(rx, gain, true, 320.0f);
    rx.change_peak_filter(true, PEAK_KEY_HZ, 10.0f);
    rx.change_peak_filter(false, PEAK_KEY_HZ, 10.0f);
    rx.change_peak_filter(true, PEAK_KEY_HZ, 10.0f);

    on_edges = 0;
    feed_shaped(rx, gain, false, 400.0f);
    REQUIRE(on_edges == 0);
}
