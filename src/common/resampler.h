#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <memory>

#include <liquid/liquid.h>

class Resampler {
    const size_t             N;
    size_t                   i = 0;
    std::unique_ptr<float[]> buf;
    firdecim_rrrf            des;

  public:
    Resampler(size_t N) : N(N), buf(std::make_unique<float[]>(N)) {
        des = firdecim_rrrf_create_kaiser(N, 7, 60.0f);
        firdecim_rrrf_set_scale(des, 1.0f / static_cast<float>(N));
    }
    ~Resampler() { firdecim_rrrf_destroy(des); }

    Resampler(const Resampler&)            = delete;
    Resampler& operator=(const Resampler&) = delete;
    Resampler(Resampler&&)                 = delete;
    Resampler& operator=(Resampler&&)      = delete;

    bool feed(float f) {
        if (i < N) {
            buf[i++] = f;
        }
        return i == N;
    }

    float execute() {
        float res;
        firdecim_rrrf_execute(des, buf.get(), &res);
        i = 0;
        return res;
    }

    size_t decim_factor() const { return N; }
};

#endif
