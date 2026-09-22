#include "cw_receiver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cw {

CwReceiver::CwReceiver(EmitTextFn emit_text, EmitOnOffFn emit_on_off)
    : decoder_(std::move(emit_text)), emit_on_off(std::move(emit_on_off)) {
    update_search_region();
    change_threshold(DEFAULT_THRESHOLD_DB);
}

void CwReceiver::update_search_region() {
    region_from_ = std::max(size_t(0), static_cast<size_t>(hpf_hz_ * static_cast<float>(FFT_SIZE) / SAMPLE_RATE));
    region_to_ = std::min(SPECTRUM_SIZE - 1, static_cast<size_t>(lpf_hz_ * static_cast<float>(FFT_SIZE) / SAMPLE_RATE));
    last_stable_bin_ = -1;
}

void CwReceiver::change_threshold(float db_val) {
    detector_.set_threshold_db(db_val);
    th_lin = std::pow(10.0f, db_val / 10.0f);
}

void CwReceiver::change_hpf_hz(float hz) {
    hpf_hz_ = hz;
    update_search_region();
}

void CwReceiver::change_lpf_hz(float hz) {
    lpf_hz_ = hz;
    update_search_region();
}

float CwReceiver::process_fft_frame_raw_llr(const std::complex<float> *fft_output, float &out_precise_freq) {
    // Power spectrum (I^2 + Q^2)
    for (size_t i = region_from_; i <= region_to_; ++i) {
        power_spectrum_[i] = std::norm(fft_output[i]);
    }

    // Coarse maximum inside the search region
    size_t max_idx = region_from_;
    float  max_val = power_spectrum_[region_from_];
    for (size_t i = region_from_ + 1; i <= region_to_; ++i) {
        if (power_spectrum_[i] > max_val) {
            max_val = power_spectrum_[i];
            max_idx = i;
        }
    }

    // Frame noise estimate: 25% percentile, then EMA-smoothed across frames.
    size_t region_length = region_to_ - region_from_ + 1;
    for (size_t i = 0; i < region_length; ++i) {
        noise_buffer_[i] = power_spectrum_[region_from_ + i];
    }
    size_t pct_offset = static_cast<size_t>(NOISE_PERCENTILE * static_cast<float>(region_length));
    if (pct_offset >= region_length) {
        pct_offset = region_length - 1;
    }
    std::nth_element(noise_buffer_.begin(), noise_buffer_.begin() + pct_offset, noise_buffer_.begin() + region_length);
    float noise_power_inst = noise_buffer_[pct_offset];

    if (noise_power_smoothed_ < 0.0f) {
        noise_power_smoothed_ = noise_power_inst;
    } else {
        noise_power_smoothed_ += NOISE_SMOOTHING_ALPHA * (noise_power_inst - noise_power_smoothed_);
    }

    // Parabolic interpolation over 3 points in log scale
    float precise_bin     = static_cast<float>(max_idx);
    float precise_max_val = max_val;

    if (max_idx > region_from_ && max_idx < region_to_) {
        float y1  = 10.0f * std::log10(power_spectrum_[max_idx - 1] + 1e-15f);
        float y2  = 10.0f * std::log10(power_spectrum_[max_idx] + 1e-15f);
        float y3  = 10.0f * std::log10(power_spectrum_[max_idx + 1] + 1e-15f);
        float den = y1 - 2.0f * y2 + y3;

        if (den < -1.5f) {
            float delta = 0.5f * (y1 - y3) / den;
            precise_bin = static_cast<float>(max_idx) + delta;
        }
    }
    out_precise_freq = precise_bin * SAMPLE_RATE / static_cast<float>(FFT_SIZE);

    float instant_snr_lin  = max_val / noise_power_smoothed_;
    bool  is_strong_enough = (instant_snr_lin >= th_lin);

    // Frequency-jump guard (reset history when retuning to a new station)
    if (last_stable_bin_ != -1) {
        bool is_frequency_jumped = std::abs(static_cast<int>(max_idx) - last_stable_bin_) >= 2;

        if (is_frequency_jumped && is_strong_enough) {
            detector_.reset();
        }
    }

    if (is_strong_enough) {
        last_stable_bin_ = static_cast<int>(max_idx);
    }

    // 6. Raw LLR of the frame
    return detector_.get_raw_llr(precise_max_val, noise_power_smoothed_);
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
    bool is_active = classifier_.is_signal_active();
    if (is_active != is_signal_detected) {
        is_signal_detected = is_active;
        emit_on_off(is_active);
    }
}

float CwReceiver::get_measured_wpm() const {
    return classifier_.get_current_wpm();
}

float CwReceiver::get_tone_freq() const {
    return current_freq_hz_;
}

} // namespace cw
