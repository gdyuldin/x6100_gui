#pragma once

#ifdef __cplusplus

#include <cmath>
#include <type_traits>

#include "type_utils.h"

template <typename T> inline T clip(T x, T min, T max) {
    if (x < min) {
        return min;
    } else if (x > max) {
        return max;
    }

    return x;
}

template <typename T> inline T align(T x, T step) {
    if (step == 0) {
        return x;
    }
    if constexpr (std::is_integral_v<T>) {
        return x - (x % step);
    } else if constexpr ((std::is_same_v<T, float> || std::is_same_v<T, double>)) {
        return x - std::fmod(x, step);
    } else {
        static_assert(always_false_v<T>, "Unsupported type passed to align");
    }
}

#endif
