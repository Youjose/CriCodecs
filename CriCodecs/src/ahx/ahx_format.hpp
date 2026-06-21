#pragma once
/**
 * @file ahx_format.hpp
 * @brief AHX frame constants and header helpers.
 *
 * Field names and frame constraints are based on the MPEG Layer II-style AHX
 * stream handled by CRI ahxencd. C++23 format helpers by Youjose.
 */

#include <bit>
#include <cstddef>
#include <cstdint>

namespace cricodecs::ahx::detail {

inline constexpr uint32_t AHX_FRAME_HEADER = 0xFFF5E0C0u;
inline constexpr uint32_t AHX_FOOTER_PREFIX = 0x00800100u;
inline constexpr uint32_t AHX_FOOTER_TAG = 0x8001000Cu;
inline constexpr size_t AHX_EXPECTED_FRAME_SIZE = 0x414;
inline constexpr size_t AHX_ENCODER_DELAY = 480;
inline constexpr int AHX_BANDS = 30;
inline constexpr int AHX_GRANULES = 12;
inline constexpr int AHX_FRAC_BITS = 28;
inline constexpr size_t AHX_SAMPLES_PER_FRAME = 1152;
inline constexpr size_t AHX_HEADER_SIZE = 0x24;

[[nodiscard]] inline constexpr bool is_ahx_frame_header(uint32_t value) noexcept {
    constexpr uint32_t fixed_mask = 0xFFFF00C0u;
    constexpr uint32_t fixed_value = 0xFFF500C0u;
    constexpr uint32_t bitrate_mask = 0x0000F000u;
    constexpr uint32_t sample_rate_mask = 0x00000C00u;
    constexpr uint32_t emphasis_mask = 0x00000003u;

    // AHX frames use a narrow MPEG Layer II mono header shape. The common CRI
    // values are 0xFFF5E0C0 and, when the padding bit is set, 0xFFF5E2C0:
    //
    //   0xFFF5E0C0 & fixed_mask -> 0xFFF500C0
    //   0xFFF5E2C0 & fixed_mask -> 0xFFF500C0
    //
    // The mask checks the static sync/version/layer/protection/channel bits,
    // then the remaining MPEG fields are range-checked below.
    // It's unclear if the above common 2 values are the only possible ones.
    if (((value ^ fixed_value) & fixed_mask) != 0u) {
        return false;
    }

    const uint32_t bitrate_index = (value & bitrate_mask) >> 12u;
    return (bitrate_index - 1u) < 14u &&
        (value & sample_rate_mask) != sample_rate_mask &&
        (value & emphasis_mask) != 0x2u;
}

[[nodiscard]] inline constexpr int64_t wrapping_add_i64(int64_t lhs, int64_t rhs) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) + std::bit_cast<uint64_t>(rhs));
}

[[nodiscard]] inline constexpr int64_t wrapping_mul_i64(int64_t lhs, int64_t rhs) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) * std::bit_cast<uint64_t>(rhs));
}

} // namespace cricodecs::ahx::detail
