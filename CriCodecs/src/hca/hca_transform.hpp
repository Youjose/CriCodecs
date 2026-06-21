#pragma once
#include "hca_format.hpp"

#include <array>

namespace cricodecs::hca::transform {

inline constexpr float HCA_DCT4_IMDCT_SCALE = 0.125f; // sqrt(2.0 / 128.0)
inline constexpr float HCA_DCT4_MDCT_SCALE = 2.0f / static_cast<float>(HCA_SAMPLES_PER_SUBFRAME);

[[nodiscard]] std::array<float, HCA_SAMPLES_PER_SUBFRAME> dct4(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& input,
    const float scale
);

} // namespace cricodecs::hca::transform
