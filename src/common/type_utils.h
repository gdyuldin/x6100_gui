#pragma once

#ifdef __cplusplus
// Always-false trait that depends on T, used to defer a static_assert in a
// template until instantiation. Single shared definition so cfg, the app and
// any future submodule can use it without importing cfg-specific headers.
template <typename> inline constexpr bool always_false_v = false;
#endif
