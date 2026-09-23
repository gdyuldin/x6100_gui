#include "cw_receiver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cw {

CwReceiver::CwReceiver(EmitTextFn emit_text, EmitOnOffFn emit_on_off, FrameFn on_frame)
    : decoder_(std::move(emit_text)), emit_on_off(std::move(emit_on_off)), on_frame_(std::move(on_frame)) {
    update_search_region();
    change_threshold(10.0f);
}

void CwReceiver::update_search_region() {
    region_from_ = std::max(size_t(0), static_cast<size_t>(hpf_hz_ * static_cast<float>(FFT_SIZE) / SAMPLE_RATE));
    region_to_ = std::min(SPECTRUM_SIZE - 1, static_cast<size_t>(lpf_hz_ * static_cast<float>(FFT_SIZE) / SAMPLE_RATE));
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

void CwReceiver::analyze_frame(const ComplexSpectrum &fft_output) {
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
        percentile_scratch_[i] = power_spectrum_[region_from_ + i];
    }
    // 25% percentile
    size_t pct_offset = static_cast<size_t>(0.25f * static_cast<float>(region_length));
    if (pct_offset >= region_length) {
        pct_offset = region_length - 1;
    }
    std::nth_element(percentile_scratch_.begin(), percentile_scratch_.begin() + pct_offset,
                     percentile_scratch_.begin() + region_length);
    float noise_power_inst = percentile_scratch_[pct_offset];

    if (noise_power_smoothed_ < 0.0f) {
        noise_power_smoothed_ = noise_power_inst;
    } else {
        noise_power_smoothed_ += 0.15f * (noise_power_inst - noise_power_smoothed_);
    }

    // Parabolic interpolation over 3 points in log scale
    float precise_bin = static_cast<float>(max_idx);

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
    current_freq_hz_ = precise_bin * SAMPLE_RATE / static_cast<float>(FFT_SIZE);

    float instant_snr_lin  = max_val / noise_power_smoothed_;
    bool  is_strong_enough = (instant_snr_lin >= th_lin);

    // Frequency-jump guard (reset history when retuning to a new station)
    if (is_strong_enough) {
        if (bin_candidate_ != -1 && std::abs(static_cast<int>(max_idx) - bin_candidate_) <= 1) {
            bin_candidate_counter_++;
        } else {
            // new candidate
            bin_candidate_         = static_cast<int>(max_idx);
            bin_candidate_counter_ = 1;
        }
    } else {
        // silence - reset
        bin_candidate_         = -1;
        bin_candidate_counter_ = 0;
    }

    // same bin was max for at least 3 frames
    if (bin_candidate_counter_ >= 3) {
        bool is_frequency_jumped = std::abs(static_cast<int>(max_idx) - last_stable_bin_) >= 2;

        if (is_frequency_jumped) {
            tone_tracker_.set_freq(current_freq_hz_);
            // Reset the level reference and the learned speed only on a real
            // retune, not on the first frequency lock (last_stable_bin_ is
            // still invalid there): a retune is almost always a new station.
            if (last_stable_bin_ >= 0) {
                level_tracker_.reset();
                classifier_.reset_speed();
            }
        }

        last_stable_bin_ = static_cast<int>(max_idx);
        bin_candidate_counter_ = 0;
    }
}

void CwReceiver::process_audio_frame(size_t n, float *samples) {
    for (size_t i = 0; i < n; ++i) {
        if (!spgram_.execute(samples[i])) {
            continue;
        }
        process_fft_frame(spgram_.get_fft_output(), spgram_.get_hop_raw());
    }
}

void CwReceiver::process_fft_frame(const ComplexSpectrum &fft_output, const RawHop &raw_hop) {
    analyze_frame(fft_output);

    float signal_level = tone_tracker_.process(raw_hop, classifier_.is_signal_active());
    float abs_llr   = detector_.get_raw_llr(signal_level, noise_power_smoothed_);
    float norm_db   = level_tracker_.norm_db(signal_level);

    Token token = classifier_.feed_frame(abs_llr, norm_db, level_tracker_.ref_valid());

    // Track the mark level only from the new classifier state, so the tracker
    // and the classifier never disagree on what counts as a signal.
    level_tracker_.observe(signal_level, classifier_.is_signal_active());

    if (token != CW_NONE) {
        decoder_.handle_token(token);
    }
    bool is_active = classifier_.is_signal_active();
    if (is_active != is_signal_detected) {
        is_signal_detected = is_active;
        emit_on_off(is_active);
    }

    if (on_frame_) {
        on_frame_();
    }
}

float CwReceiver::get_measured_wpm() const {
    return classifier_.get_current_wpm();
}

float CwReceiver::get_tone_freq() const {
    return current_freq_hz_;
}

} // namespace cw
