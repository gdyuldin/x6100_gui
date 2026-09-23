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
// cw_bayes and its tests.
constexpr float  SAMPLE_RATE   = 4000.0f;
constexpr size_t FFT_SIZE      = 128;
constexpr size_t HOP_SIZE      = FFT_SIZE / 4;                                    // one frame hop
constexpr size_t SPECTRUM_SIZE = FFT_SIZE / 2 + 1;                                // non-negative half
constexpr int    BIN_SIZE_MS   = static_cast<int>(HOP_SIZE * 1000 / SAMPLE_RATE); // ms per frame
constexpr size_t HIST_BINS     = 150;
constexpr int    WPM_K         = 1200; // dot ms = WPM_K / wpm

// Spectrum types shared by SpgramReal, CwReceiver and the tests.
using ComplexSpectrum = std::array<std::complex<float>, SPECTRUM_SIZE>; // raw FFT (amplitudes)
using PowerSpectrum   = std::array<float, SPECTRUM_SIZE>;               // |X|^2 per absolute bin
using RegionScratch   = std::array<float, SPECTRUM_SIZE>; // nth_element copy; first region_length entries used

} // namespace cw
