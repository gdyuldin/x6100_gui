#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <liquid/liquid.h>

#include "cw_config.h"

namespace cw {

// Baseband low-pass cutoff of the coherent detector, in Hz.
constexpr float BB_FILTER_FREQ = 75.0f;

// Coherent CW tone tracker: down-converts the newest audio hop to baseband with
// an NCO, low-passes and averages it, and locks the NCO to the tone phase with
// a PLL while the keying signal is present. Returns the baseband power.
class CoherentToneTracker {

    nco_crcf     nco = nullptr;
    iirfilt_crcf flt = nullptr;

    std::complex<float> prev_avg{}; // unit-magnitude mean of the previous frame
    float               prev_power = 0.0f;

  public:
    CoherentToneTracker(const CoherentToneTracker &)            = delete;
    CoherentToneTracker &operator=(const CoherentToneTracker &) = delete;
    CoherentToneTracker(CoherentToneTracker &&)                 = delete;
    CoherentToneTracker &operator=(CoherentToneTracker &&)      = delete;

    CoherentToneTracker() {
        nco = nco_crcf_create(LIQUID_NCO);
        flt = iirfilt_crcf_create_lowpass(2, BB_FILTER_FREQ / SAMPLE_RATE);

        if (nco) {
            // usual range - 0.01f - 0.05f
            nco_crcf_pll_set_bandwidth(nco, 0.02f);
            set_freq(700.0f);
        }
    }

    ~CoherentToneTracker() {
        if (nco) {
            nco_crcf_destroy(nco);
            nco = nullptr;
        }
        if (flt) {
            iirfilt_crcf_destroy(flt);
            flt = nullptr;
        }
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

        // Drop the stale phase/power reference so the first frame after retune
        // does not kick the PLL with data from the old frequency.
        prev_avg   = {};
        prev_power = 0.0f;
    }

    float process(const RawHop &data, bool is_on) {
        if (!nco || !flt) {
            return 0.0f;
        }

        // Move to baseband
        std::array<std::complex<float>, HOP_SIZE> bb_data;
        for (size_t i = 0; i < data.size(); i++) {
            float sin_val, cos_val;
            nco_crcf_sincos(nco, &sin_val, &cos_val);
            bb_data[i] = {data[i] * cos_val, -data[i] * sin_val};
            nco_crcf_step(nco);
        }

        // Apply filter
        iirfilt_crcf_execute_block(flt, bb_data.data(), bb_data.size(), bb_data.data());

        // Average
        std::complex<float> avg{};
        for (auto &&i : bb_data) {
            avg += i;
        }
        avg /= bb_data.size();

        float power = std::norm(avg);

        // Update NCO (add check, that tracker active). The mean is normalized so
        // the cross product is sin(phase error) independent of the signal level;
        // prev_avg keeps the unit-magnitude mean of the previous frame.
        if (power > 1e-8f) {
            std::complex<float> avg_n = avg / std::sqrt(power);
            if (is_on && (power * 4.0f > prev_power)) {
                float phase_error = (avg_n.imag() * prev_avg.real()) - (avg_n.real() * prev_avg.imag());
                nco_crcf_pll_step(nco, phase_error);
            }
            prev_avg = avg_n;
        }
        prev_power = power;
        return power;
    }
};

} // namespace cw
