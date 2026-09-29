#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <liquid/liquid.h>

#include "cw_config.h"

namespace cw {

// Baseband low-pass cutoff of the coherent detector, in Hz.
constexpr float BB_FILTER_FREQ = 100.0f;
constexpr size_t NTAPS = 81;

// Alive/PLL tuning. The phase/aliveness update runs every ALIVE_DECIM
// integrators (32 ms) on their mean; liveness is an EMA of the coherent power
// (~256 ms) compared against the aligned noise, and a retune of the NCO is
// vetoed while the current tone is alive.
constexpr int   ALIVE_DECIM    = 4;      // integrators per alive/PLL update
constexpr float ALIVE_EMA      = 0.125f; // power EMA, ~256 ms
constexpr float VETO_THRESHOLD = 4.0f;   // ema_power4_ / noise4_ above which alive
constexpr int   VETO_LENGTH    = 32;     // windows below threshold => retune allowed (1 s)
constexpr float PLL_WEIGHT_LO  = 2.0f;   // r4 at which the PLL weight is 0
constexpr float PLL_WEIGHT_HI  = 5.0f;  // r4 at which the PLL weight is 1
constexpr float RETUNE_BINS    = 2.0f;   // retune hysteresis vs the FFT bin

// Coherent CW tone tracker: down-converts the newest audio hop to baseband with
// an NCO, low-passes and averages it, and locks the NCO to the tone phase with
// a PLL while the keying signal is present. Returns the baseband power.
class CoherentToneTracker {
    nco_crcf     nco = nullptr;
    firfilt_crcf flt = nullptr;
    windowcf     buffer_ = nullptr;

    size_t n_samples_ = 0; // samples since the last integrator output
    size_t n_pll_     = 0; // integrator outputs since the last alive/PLL update
    std::complex<float> acc_pll{}; // mean of the current ALIVE_DECIM integrators

    std::complex<float> prev_avg_{}; // unit-magnitude mean of the previous update window

    float noise4_        = 0.0f; // aligned per-window noise power; <= 0 => unknown
    float ema_power4_    = 0.0f;
    bool  ema_seeded_    = false;
    int   alive_counter_ = VETO_LENGTH; // veto starts expired: the first lock may retune
    bool  was_alive_     = false;

    // Advances the liveness/PLL state on the mean of ALIVE_DECIM integrators.
    void update_alive_and_pll(const std::complex<float> &avg4) {
        const float power4 = std::norm(avg4);

        if (noise4_ <= 0.0f) {
            return; // noise not measured yet: cannot judge liveness
        }

        if (!ema_seeded_) {
            ema_power4_ = power4;
            ema_seeded_ = true;
        } else {
            ema_power4_ += ALIVE_EMA * (power4 - ema_power4_);
        }

        const float r4    = power4 / noise4_;
        const bool  alive = (ema_power4_ / noise4_) > VETO_THRESHOLD;

        if (alive) {
            alive_counter_ = 0;
        } else if (alive_counter_ < VETO_LENGTH) {
            alive_counter_++;
        }

        if (power4 > 1e-8f) {
            const std::complex<float> avg_n = avg4 / std::sqrt(power4);
            // Step the PLL only while the tone stays alive and only between two
            // consecutive alive windows; the first window after a gap just seeds
            // the phase reference, so noise cannot kick the NCO.
            if (was_alive_ && alive) {
                float w = (r4 - PLL_WEIGHT_LO) / (PLL_WEIGHT_HI - PLL_WEIGHT_LO);
                w       = std::clamp(w, 0.0f, 1.0f);
                if (w > 0.0f) {
                    float phase_error = (avg_n.imag() * prev_avg_.real()) - (avg_n.real() * prev_avg_.imag());
                    nco_crcf_pll_step(nco, w * phase_error);
                }
            }
            prev_avg_ = avg_n;
        }
        was_alive_ = alive;
    }

  public:
    CoherentToneTracker(const CoherentToneTracker &)            = delete;
    CoherentToneTracker &operator=(const CoherentToneTracker &) = delete;
    CoherentToneTracker(CoherentToneTracker &&)                 = delete;
    CoherentToneTracker &operator=(CoherentToneTracker &&)      = delete;

    CoherentToneTracker() {
        nco = nco_crcf_create(LIQUID_NCO);
        float fc =  BB_FILTER_FREQ / SAMPLE_RATE;
        flt = firfilt_crcf_create_kaiser(NTAPS, fc, 60.0f, 0.0f);
        firfilt_crcf_set_scale(flt, 2.0f*fc);

        if (nco) {
            nco_crcf_pll_set_bandwidth(nco, 0.001f);
            set_freq(670.0f);
        }
        buffer_ = windowcf_create(COHERENT_INTEGRATOR_SIZE);

        if (!nco || !flt || !buffer_) {
            fprintf(stderr, "cw: failed to create NCO/filter/window\n");
        }
    }

    ~CoherentToneTracker() {
        if (nco) {
            nco_crcf_destroy(nco);
            nco = nullptr;
        }
        if (flt) {
            firfilt_crcf_destroy(flt);
            flt = nullptr;
        }
        if (buffer_) {
            windowcf_destroy(buffer_);
            buffer_ = nullptr;
        }
    }

    // Current NCO frequency in Hz (positive); 0 if the NCO is not allocated.
    float get_freq_hz() const {
        if (!nco)
            return 0.0f;
        return -nco_crcf_get_frequency(nco) * SAMPLE_RATE / (2.0f * static_cast<float>(M_PI));
    }

    // Noise power per ALIVE_DECIM-integrator window, in the tracker's units.
    void set_noise(float noise_integr) {
        noise4_ = noise_integr * NOISE4_SCALE;
    }

    void set_freq(float freq) {
        if (!nco) {
            return;
        }
        // Reset preserving phase
        float current_phase = nco_crcf_get_phase(nco);
        nco_crcf_reset(nco);
        nco_crcf_set_frequency(nco, 2.0f * static_cast<float>(M_PI) * (-freq / SAMPLE_RATE));
        nco_crcf_set_phase(nco, current_phase);

        // Drop the stale phase/power reference and the filter/window state so
        // the first window after a retune carries no data from the old
        // frequency (the window holds COHERENT_INTEGRATOR_SIZE baseband samples
        // and the FIR its history).
        if (flt) {
            firfilt_crcf_reset(flt);
        }
        if (buffer_) {
            windowcf_reset(buffer_);
        }
        n_samples_  = 0;
        n_pll_      = 0;
        acc_pll     = {};
        prev_avg_   = {};
        ema_seeded_ = false;
        was_alive_  = false;
    }

    // Frequency policy: retune to the observed peak once it has been stable for
    // STABLE_FRAMES_MIN frames, it is clearly off the NCO (RETUNE_BINS) and the
    // current tone has not been alive for VETO_LENGTH windows. Returns true when
    // the NCO was retuned.
    bool consider_retune(const FrameObservation &obs) {
        if (!nco) {
            return false;
        }
        if (obs.stable_frames < STABLE_FRAMES_MIN) {
            return false;
        }
        if (alive_counter_ < VETO_LENGTH) {
            return false;
        }
        const float bin_hz = SAMPLE_RATE / static_cast<float>(FFT_SIZE);
        if (std::fabs(obs.freq_hz - get_freq_hz()) < RETUNE_BINS * bin_hz) {
            return false;
        }
        set_freq(obs.freq_hz);
        return true;
    }

    float process(float sample) {
        if (!nco || !flt || !buffer_) {
            return -1.0f;
        }

        // Move to baseband
        float sin_val, cos_val;
        nco_crcf_sincos(nco, &sin_val, &cos_val);
        std::complex<float> bb_sample = {sample * cos_val, -sample * sin_val};
        nco_crcf_step(nco);

        // Apply filter
        firfilt_crcf_push(flt, bb_sample);
        firfilt_crcf_execute(flt, &bb_sample);

        // Put to buffer
        windowcf_push(buffer_, bb_sample);
        n_samples_++;

        if (n_samples_ >= COHERENT_INTEGRATOR_HOP) {
            n_samples_ = 0;
            std::complex<float> *bb_samples;
            if (windowcf_read(buffer_, &bb_samples) != LIQUID_OK) {
                return -1.0f;
            }
            // Average
            std::complex<float> avg{};
            for (size_t i = 0; i < COHERENT_INTEGRATOR_SIZE; i++) {
                avg += bb_samples[i];
            }
            avg /= COHERENT_INTEGRATOR_SIZE;

            float power = std::norm(avg);

            acc_pll += avg;
            if (++n_pll_ >= ALIVE_DECIM) {
                n_pll_ = 0;
                std::complex<float> avg4 = acc_pll / static_cast<float>(ALIVE_DECIM);
                acc_pll = {};
                update_alive_and_pll(avg4);
            }

            return power;
        }
        return -1.0f;
    }
};

} // namespace cw
