#pragma once

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

// Public DSP constants of the decoder. SAMPLE_RATE is exposed so external code
// can configure the audio stream to match the decoder (see cw_types.h note).
constexpr size_t FFT_SIZE      = 128;
constexpr size_t SPECTRUM_SIZE = FFT_SIZE / 2 + 1;
constexpr float  SAMPLE_RATE   = 4000.0f;
constexpr size_t HIST_BINS     = 150;
constexpr int    BIN_SIZE_MS   = 8;
constexpr int    WPM_K         = 1200;  // dot ms = WPM_K / wpm

} // namespace cw
