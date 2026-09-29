#pragma once

#include <array>
#include <complex>
#include <cstddef>

namespace cw {

// Morse tokens handed from the timing classifier to the decoder tree.
enum Token {
    CW_NONE,
    CW_DOT,
    CW_DASH,
    CW_ELEMENT_SPACE,
    CW_LETTER_SPACE,
    CW_WORD_SPACE
};

// Module-internal DSP constants. Only CwReceiver::SAMPLE_RATE is public: it is
// the rate the receiver must be fed with. Everything else here is private to
// cw_decoder and its tests. Component-specific tuning lives with the component
// that uses it (see time_classifier.h / coherent_tone_tracker.h).
constexpr float  SAMPLE_RATE   = 4000.0f;
constexpr size_t FFT_SIZE      = 256;
constexpr size_t FFT_HOP       = 64;
constexpr size_t SPECTRUM_SIZE = FFT_SIZE / 2 + 1;               // non-negative half
constexpr size_t COHERENT_INTEGRATOR_SIZE = 64;                  // 16 ms
constexpr size_t COHERENT_INTEGRATOR_HOP = 32;                   // 8 ms

// The classifier is fed once per coherent integrator hop, so that hop is its
// frame duration (the histogram resolution is half of it).
constexpr int    BIN_SIZE_MS   = static_cast<int>(COHERENT_INTEGRATOR_HOP * 1000 / SAMPLE_RATE);
constexpr int    WPM_K         = 1200; // dot ms = WPM_K / wpm

// Converts the 25% percentile of the FFT power spectrum into the per-bin noise
// power (1/log(4/3) undoes the percentile ordering) and then into the coherent
// integrator's noise units. The 1.5 is the Hann window's equivalent noise
// bandwidth (ENBW), in FFT bins.
constexpr float NOISE_ALIGN = (1.0f / 0.28768207f) * static_cast<float>(FFT_SIZE) /
                              (1.5f * static_cast<float>(COHERENT_INTEGRATOR_SIZE));
// Averaging 4 half-overlapping integrators reduces the noise power by less than
// 4; 7/16 is the effective factor for 50% overlap.
constexpr float NOISE4_SCALE = 7.0f / 16.0f;

// Candidate gate of the FFT peak: independent of the user floor, so frequency
// tracking cannot be dragged by a raised detection floor.
constexpr float FFT_STRONG_DB = 15.5f; // ~36 linear
// Consecutive strong frames needed before the frequency may be retuned.
constexpr int STABLE_FRAMES_MIN = 2;

// One frame's measurements from CwReceiver::analyze_frame. It carries only
// measurements; retune/veto and classifier reset are policy applied by the
// caller (CwReceiver).
struct FrameObservation {
    float freq_hz;       // peak frequency, parabolic interpolation
    float snr_lin;       // peak power / per-bin noise power
    int   stable_frames; // consecutive strong frames with the peak near the same bin
};

// Spectrum types shared by SpgramReal, CwReceiver and the tests.
using ComplexSpectrum = std::array<std::complex<float>, SPECTRUM_SIZE>; // raw FFT (amplitudes)
using PowerSpectrum   = std::array<float, SPECTRUM_SIZE>;               // |X|^2 per absolute bin
using RegionScratch   = std::array<float, SPECTRUM_SIZE>; // nth_element copy; first region_length entries used

} // namespace cw
