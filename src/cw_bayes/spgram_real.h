#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>

#include <liquid/liquid.h>

#include "cw_config.h"

namespace cw {

// Streaming sliding-window FFT for the real-valued CW audio stream. Emits the
// non-negative half of the spectrum (bin 0 = DC, bin SPECTRUM_SIZE-1 = Nyquist)
// every HOP_SIZE input samples. Coupled to the module constants: it is not a
// reusable generic component.
class SpgramReal {

    const size_t step_   = HOP_SIZE;
    windowf      buffer_ = NULL;
    fftplan      fft_    = NULL;

    size_t                                    n_samples_ = 0;
    std::array<std::complex<float>, FFT_SIZE> buf_time_;
    std::array<std::complex<float>, FFT_SIZE> buf_freq_;
    std::array<float, FFT_SIZE>               window_;
    ComplexSpectrum                           fft_output_;

  public:
    SpgramReal(const SpgramReal &)            = delete;
    SpgramReal &operator=(const SpgramReal &) = delete;
    SpgramReal(SpgramReal &&)                 = delete;
    SpgramReal &operator=(SpgramReal &&)      = delete;

    SpgramReal() {
        fft_    = fft_create_plan(FFT_SIZE, buf_time_.data(), buf_freq_.data(), LIQUID_FFT_FORWARD, 0);
        buffer_ = windowf_create(FFT_SIZE);

        if (fft_ == NULL || buffer_ == NULL) {
            fprintf(stderr, "cw: failed to create FFT plan/window\n");
        }

        /* Fill window with the same energy normalization as ChunkedSpgram
         * (src/dsp.cpp): g = 1 / sqrtf(sum(w * w) * NFFT / window_size), and
         * here the window covers the whole FFT (window_size == FFT_SIZE). */
        float sum = 0.0f;
        for (size_t i = 0; i < FFT_SIZE; i++) {
            window_[i] = liquid_hann(i, FFT_SIZE);
            sum += window_[i] * window_[i];
        }
        float g = 1.0f / sqrtf(sum);
        // scale window
        for (size_t i = 0; i < FFT_SIZE; i++)
            window_[i] *= g;
    };

    ~SpgramReal() {
        if (buffer_) {
            windowf_destroy(buffer_);
        }
        if (fft_) {
            fft_destroy_plan(fft_);
        }
    };

    bool execute(float sample) {
        if (fft_ == NULL || buffer_ == NULL) {
            return false;
        }
        windowf_push(buffer_, sample);
        n_samples_++;
        if (n_samples_ < step_) {
            return false;
        }
        n_samples_ = 0;

        float *rc;
        if (windowf_read(buffer_, &rc) != LIQUID_OK) {
            return false;
        }
        for (size_t i = 0; i < FFT_SIZE; i++) {
            buf_time_[i] = rc[i] * window_[i];
        }
        fft_execute(fft_);

        /* Keep the non-negative half of the real-signal spectrum: bin 0 is DC,
         * bin SPECTRUM_SIZE-1 is Nyquist. */
        for (size_t i = 0; i < SPECTRUM_SIZE; i++) {
            fft_output_[i] = buf_freq_[i];
        }
        return true;
    };
    const ComplexSpectrum &get_fft_output() { return fft_output_; }
};

} // namespace cw
