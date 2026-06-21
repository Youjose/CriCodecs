#pragma once
/**
 * @file ahx_codec.hpp
 * @brief Public AHX codec surface.
 *
 * AHX is CRI's MPEG Layer II-style mono codec used through the ADX family.
 * The current public surface is the CriCodecs C++23 API; implementation
 * details are split between the decoder, encoder, format helpers, and MPEG/CRI
 * table headers.
 *
 * Attribution:
 * - Early decode behavior was initially based on the public radx AHX decoder.
 * - Current frame behavior, encoder policy, and symbol naming are checked
 *   against CRI's ahxencd binaries.
 * - The subband filterbank and quantizer model follow MPEG Layer II.
 * - CriCodecs C++23 port and follow-up reverse engineering by Youjose.
 */

#include "ahx_bit_allocation.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "../wav/wav_container.hpp"

namespace cricodecs::ahx {

using AhxError = std::string;

struct AhxKey {
    uint16_t start = 0;
    uint16_t mult = 0;
    uint16_t add = 0;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return start == 0 && mult == 0 && add == 0;
    }
};

struct AhxDecodeConfig {
    uint8_t encoding_mode = 0;
    uint32_t sample_rate = 0;
    uint32_t sample_count = 0;
    uint8_t channels = 0;
    uint8_t encryption_type = 0;
    size_t start_offset = 0;
    AhxKey key{};
};

struct AhxEncodeConfig {
    uint8_t encoding_mode = 0x10;
    uint32_t sample_rate = 22050;
    uint8_t channels = 1;
    uint8_t encryption_type = 0;
    AhxKey key{};
    AhxBitAllocationPattern bit_allocation_pattern = default_bit_allocation_pattern();
};

std::expected<std::vector<int16_t>, AhxError> decode(
    std::span<const uint8_t> file_data,
    const AhxDecodeConfig& config
);

std::expected<std::vector<uint8_t>, AhxError> decrypt(
    std::span<const uint8_t> file_data,
    const AhxDecodeConfig& config
);

std::expected<std::vector<uint8_t>, AhxError> encode(
    std::span<const int16_t> pcm_data,
    const AhxEncodeConfig& config
);
std::expected<std::vector<uint8_t>, AhxError> encode(
    const wav::WavContainer& wav,
    const AhxEncodeConfig& config
);

} // namespace cricodecs::ahx
