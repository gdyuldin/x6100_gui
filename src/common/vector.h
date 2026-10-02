#pragma once

#include <stdint.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

static inline void vector_s16_to_f(const int16_t *src, float *dst, size_t count) {

    const float scale = 1.0f / (1 << 15);

    size_t i = 0;
#ifdef __ARM_NEON
    float32x4_t v_scale = vdupq_n_f32(scale);
    for (; i + 4 <= count; i += 4) {
        int16x4_t   in16    = vld1_s16(&src[i]);
        int32x4_t   in32    = vmovl_s16(in16);
        float32x4_t out_f32 = vcvtq_f32_s32(in32);
        out_f32             = vmulq_f32(out_f32, v_scale);
        vst1q_f32(&dst[i], out_f32);
    }
#endif
    // rest part
    for (; i < count; i++) {
        dst[i] = scale * src[i];
    }
}

static inline void vector_f_to_s16(const float *src, int16_t *dst, size_t count) {
    const float scale = (1 << 15);
    size_t      i     = 0;
#ifdef __ARM_NEON
    float32x4_t v_scale = vdupq_n_f32(scale);
    for (; i + 4 <= count; i += 4) {
        float32x4_t in_f32 = vld1q_f32(&src[i]);
        in_f32             = vmulq_f32(in_f32, v_scale);
        int32x4_t in32     = vcvtq_s32_f32(in_f32);
        int16x4_t out16    = vqmovn_s32(in32);
        vst1_s16(&dst[i], out16);
    }
#endif
    size_t rem = count - i;
    for (size_t k = 0; k < rem; k++) {
        float val = scale * src[i + k];

        // Check for NaN
        if (val != val)
            val = 0.0f;
        else if (val > 32767.0f)
            val = 32767.0f;
        else if (val < -32768.0f)
            val = -32768.0f;

        dst[i + k] = (int16_t)val;
    }
}
