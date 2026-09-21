/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "cw_receiver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cw {

CwReceiver::CwReceiver(float hpf_hz, float lpf_hz, float initial_threshold_db, EmitFn emit)
    : region_from_(std::max(size_t(0), static_cast<size_t>(hpf_hz * static_cast<float>(FFT_SIZE) / SAMPLE_RATE))),
      region_to_(std::min(SPECTRUM_SIZE - 1, static_cast<size_t>(lpf_hz * static_cast<float>(FFT_SIZE) / SAMPLE_RATE))),
      decoder_(std::move(emit)) {
    detector_.set_threshold_db(initial_threshold_db);
}

void CwReceiver::change_sensitivity(float db_val) {
    detector_.set_threshold_db(db_val);
}

float CwReceiver::process_fft_frame_raw_llr(const std::complex<float> *fft_output, float &out_precise_freq) {
    // 1. Power spectrum (I^2 + Q^2)
    for (size_t i = region_from_; i <= region_to_; ++i) {
        power_spectrum_[i] = std::norm(fft_output[i]);
    }

    // 2. Coarse maximum inside the search region
    size_t max_idx = region_from_;
    float  max_val = power_spectrum_[region_from_];
    for (size_t i = region_from_ + 1; i <= region_to_; ++i) {
        if (power_spectrum_[i] > max_val) {
            max_val = power_spectrum_[i];
            max_idx = i;
        }
    }

    // 3. Parabolic interpolation over 3 points in log scale
    float precise_bin = static_cast<float>(max_idx);
    float precise_max_val = max_val;
    if (max_idx > region_from_ && max_idx < region_to_) {
        float y1  = 10.0f * std::log10(power_spectrum_[max_idx - 1] + 1e-15f);
        float y2  = 10.0f * std::log10(power_spectrum_[max_idx] + 1e-15f);
        float y3  = 10.0f * std::log10(power_spectrum_[max_idx + 1] + 1e-15f);
        float den = y1 - 2.0f * y2 + y3;
        if (std::fabs(den) > 1e-5f) {
            precise_bin = static_cast<float>(max_idx) + 0.5f * (y1 - y3) / den;
            float precise_val_db = y2 - 0.25f * std::pow(y1 - y3, 2.0f) / den;
            precise_max_val = std::pow(10.0f, precise_val_db / 10.0f);
        }
    }
    out_precise_freq = precise_bin * SAMPLE_RATE / static_cast<float>(FFT_SIZE);

    // 4. Frame noise median (std::nth_element over a fixed stack buffer)
    size_t region_length = region_to_ - region_from_ + 1;
    for (size_t i = 0; i < region_length; ++i) {
        median_buffer_[i] = power_spectrum_[region_from_ + i];
    }
    size_t mid_offset = region_length / 2;
    std::nth_element(median_buffer_.begin(), median_buffer_.begin() + mid_offset,
                     median_buffer_.begin() + region_length);
    float noise_power = median_buffer_[mid_offset];

    // 5. Frequency-jump guard (reset history when retuning to a new station)
    if (last_stable_bin_ != -1 && std::abs(static_cast<int>(max_idx) - last_stable_bin_) >= 2) {
        detector_.reset();
    }
    last_stable_bin_ = static_cast<int>(max_idx);

    // 6. Raw LLR of the frame
    return detector_.get_raw_llr(precise_max_val, noise_power);
}

void CwReceiver::process_audio_frame(const std::complex<float> *fft_output) {
    // Step 1: run the frame through the DSP. Yields the raw LLR and the precise
    // peak frequency (usable later for display or NCO tuning).
    float frame_llr = process_fft_frame_raw_llr(fft_output, current_freq_hz_);

    // Step 2: feed the LLR to the adaptive timing block. It returns a token
    // only at the end of a physical interval (mark or space).
    Token token = classifier_.feed_frame_llr(frame_llr);

    // Step 3: a formed token goes to the Morse tree
    if (token != CW_NONE) {
        decoder_.handle_token(token);
    }
}

float CwReceiver::get_measured_wpm() const {
    return classifier_.get_current_wpm();
}

} // namespace cw
