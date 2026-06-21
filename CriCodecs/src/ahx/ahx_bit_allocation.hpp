#pragma once
/**
 * @file ahx_bit_allocation.hpp
 * @brief AHX bit-allocation helpers for the MPEG Layer II-style codec core.
 *
 * The codec model follows MPEG Layer II allocation behavior as observed through
 * CRI ahxencd and various samples. C++23 table shaping by Youjose.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace cricodecs::ahx {

using AhxBitAllocationPattern = std::array<uint8_t, 32>;

enum class AhxBitAllocationPreset : uint8_t {
    default_pattern = 0,
    preset_22050,
    preset_24000,
    preset_44100,
    preset_48000,
};

[[nodiscard]] constexpr uint8_t max_bit_allocation_value_for_band(size_t band) noexcept {
    return
        band < 4 ? 15 :
        band < 11 ? 7 :
        band < 30 ? 3 : 0;
}

[[nodiscard]] constexpr AhxBitAllocationPattern default_bit_allocation_pattern() noexcept {
    return {
        6, 6, 6, 6, 4, 4, 3, 3,
        3, 3, 3, 3, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 0, 0,
    };
}

[[nodiscard]] constexpr AhxBitAllocationPattern preset_bit_allocation_pattern(AhxBitAllocationPreset preset) noexcept {
    switch (preset) {
        case AhxBitAllocationPreset::preset_22050:
            return {
                6, 6, 6, 6, 6, 6, 6, 6,
                6, 6, 6, 3, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 0, 0, 0,
            };
        case AhxBitAllocationPreset::preset_24000:
            return {
                6, 6, 6, 6, 6, 6, 6, 6,
                6, 3, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 0, 0, 0, 0,
            };
        case AhxBitAllocationPreset::preset_44100:
            return {
                6, 6, 6, 6, 6, 3, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
            };
        case AhxBitAllocationPreset::preset_48000:
            return {
                6, 6, 6, 6, 3, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
            };
        case AhxBitAllocationPreset::default_pattern:
        default:
            return default_bit_allocation_pattern();
    }
}

[[nodiscard]] constexpr AhxBitAllocationPattern clamp_bit_allocation_pattern(AhxBitAllocationPattern pattern) noexcept {
    for (size_t band = 0; band < pattern.size(); ++band) {
        const uint8_t max_value = max_bit_allocation_value_for_band(band);
        if (pattern[band] > max_value) {
            pattern[band] = max_value;
        }
    }
    return pattern;
}

} // namespace cricodecs::ahx
