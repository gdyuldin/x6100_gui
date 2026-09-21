/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <array>
#include <complex>

#include "cw_types.h"
#include "morse_decoder.h"
#include "power_detector.h"
#include "time_classifier.h"

namespace cw {

// Application-facing entry point. Owns the FFT-frame DSP pipeline and the three
// sub-components (power detector, timing classifier, Morse decoder).
class CwReceiver {
  public:
    using EmitFn = MorseDecoder::EmitFn;

    CwReceiver(float hpf_hz, float lpf_hz, float initial_threshold_db, EmitFn emit);

    // Must be called every BIN_SIZE_MS (10 ms) with SPECTRUM_SIZE complex bins
    // (right half of a real FFT of FFT_SIZE).
    void process_audio_frame(const std::complex<float> *fft_output);

    float get_measured_wpm() const;
    void  change_sensitivity(float db_val);

  private:
    // Returns the raw LLR of the frame and writes the precise peak frequency.
    float process_fft_frame_raw_llr(const std::complex<float> *fft_output, float &out_precise_freq);

    size_t                           region_from_     = 0;
    size_t                           region_to_       = 0;
    int                              last_stable_bin_ = -1;
    PowerDetector                    detector_;
    TimeClassifier                   classifier_;
    MorseDecoder                     decoder_;
    std::array<float, SPECTRUM_SIZE> power_spectrum_{};
    std::array<float, SPECTRUM_SIZE> median_buffer_{};
    float                            current_freq_hz_ = 0.0f;
};

} // namespace cw
