#pragma once
/**
 * @file ahx_allocation_tables.hpp
 * @brief MPEG Layer II/AHX allocation and quantization tables.
 *
 * These tables describe the fixed AHX allocation bit widths, quantizer classes,
 * and CRI transmission-pattern policy used by the frame encoder/decoder.
 * Generated entries are checked in ahx_table_checks.hpp against values carried
 * by the earlier radx port and CRI ahxencd reverse-engineering notes.
 *
 * Attribution:
 * - Quantizer/allocation model: MPEG Layer II.
 * - Transmission-pattern policy and .bap behavior: CRI ahxencd.
 * - CriCodecs C++23 table generation and checks by Youjose.
 */

#include "ahx_format.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace cricodecs::ahx::detail {

struct AhxDecodeQuantSpec {
    int64_t nlevels = 0;
    uint32_t group = 0;
    uint32_t bits = 0;
    int64_t c = 0;
    int64_t d = 0;
};

struct AhxEncodeQuantSpec {
    int64_t nlevels = 0;
    uint32_t group = 0;
    uint32_t bits = 0;
    int64_t c = 0;
    int64_t d = 0;
    uint32_t code_count = 0;
    int64_t min_level = 0;
    int64_t max_level = 0;
    int64_t step = 1;
};

struct AhxQuantClass {
    int64_t nlevels = 0;
    uint32_t group = 0;
    uint32_t bits = 0;
};

consteval std::array<uint8_t, 32> generate_bitalloc_table() {
    std::array<uint8_t, 32> table{};
    for (size_t band = 0; band < table.size(); ++band) {
        table[band] =
            band < 4 ? 4 :
            band < 11 ? 3 :
            band < 30 ? 2 : 0;
    }
    return table;
}

consteval std::array<std::array<uint8_t, 16>, 5> generate_offset_table() {
    std::array<std::array<uint8_t, 16>, 5> table{};
    for (size_t bits = 2; bits <= 4; ++bits) {
        const size_t count = bits < 4 ? (1u << bits) : (1u << bits) - 1u;
        for (size_t allocation = 1; allocation <= count; ++allocation) {
            uint8_t index = static_cast<uint8_t>(allocation - 1u);
            if (bits < 4 && allocation >= 3) {
                ++index;
            }
            table[bits][allocation - 1u] = index;
        }
    }
    return table;
}

[[nodiscard]] consteval int64_t quant_c(int64_t nlevels) {
    const uint64_t quant_range = std::bit_ceil(static_cast<uint64_t>(nlevels));
    const uint64_t numerator = quant_range << 28u;
    return static_cast<int64_t>((numerator + static_cast<uint64_t>(nlevels / 2)) / static_cast<uint64_t>(nlevels));
}

[[nodiscard]] consteval int64_t quant_d(uint32_t group, uint32_t bits) {
    if (group != 0) {
        return 1ll << 27u;
    }
    return 1ll << (29u - bits);
}

template <size_t N>
consteval std::array<AhxDecodeQuantSpec, N> generate_decode_quant_table(
    const std::array<AhxQuantClass, N>& classes)
{
    std::array<AhxDecodeQuantSpec, N> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = AhxDecodeQuantSpec{
            .nlevels = classes[i].nlevels,
            .group = classes[i].group,
            .bits = classes[i].bits,
            .c = quant_c(classes[i].nlevels),
            .d = quant_d(classes[i].group, classes[i].bits),
        };
    }
    return table;
}

[[nodiscard]] consteval uint32_t quant_sample_bits(const AhxDecodeQuantSpec& quant) {
    return quant.group != 0 ? quant.group : quant.bits;
}

[[nodiscard]] consteval uint32_t quant_code_count(const AhxDecodeQuantSpec& quant) {
    return quant.group != 0 ? static_cast<uint32_t>(quant.nlevels) : (1u << quant.bits);
}

[[nodiscard]] consteval int64_t reconstruct_quantized_sample(const AhxDecodeQuantSpec& quant, uint32_t code) {
    const uint32_t sample_bits = quant_sample_bits(quant);
    int64_t requantized = static_cast<int64_t>(code) ^ (1LL << (sample_bits - 1));
    requantized |= -(requantized & (1LL << (sample_bits - 1)));
    requantized <<= (AHX_FRAC_BITS - (sample_bits - 1));
    return wrapping_mul_i64(wrapping_add_i64(requantized, quant.d), quant.c) >> AHX_FRAC_BITS;
}

template <size_t N>
consteval std::array<AhxEncodeQuantSpec, N> generate_encode_quant_table(
    const std::array<AhxDecodeQuantSpec, N>& decode_table)
{
    std::array<AhxEncodeQuantSpec, N> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        const AhxDecodeQuantSpec& quant = decode_table[i];
        const uint32_t code_count = quant_code_count(quant);
        const int64_t min_level = reconstruct_quantized_sample(quant, 0);
        const int64_t max_level = reconstruct_quantized_sample(quant, code_count - 1);
        const int64_t next_level = reconstruct_quantized_sample(quant, 1);
        const int64_t step = next_level > min_level ? next_level - min_level : 1;
        table[i] = AhxEncodeQuantSpec{
            .nlevels = quant.nlevels,
            .group = quant.group,
            .bits = quant.bits,
            .c = quant.c,
            .d = quant.d,
            .code_count = code_count,
            .min_level = min_level,
            .max_level = max_level,
            .step = step,
        };
    }
    return table;
}

template <size_t N>
consteval std::array<int8_t, N> generate_qbits_table(
    const std::array<AhxQuantClass, N>& classes)
{
    std::array<int8_t, N> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<int8_t>(classes[i].group != 0
            ? -static_cast<int>(classes[i].bits)
            : static_cast<int>(classes[i].bits));
    }
    return table;
}

template <size_t N>
consteval std::array<std::array<int64_t, 3>, N> generate_grouped_decode_table(
    const AhxDecodeQuantSpec& quant)
{
    std::array<std::array<int64_t, 3>, N> table{};
    for (size_t code = 0; code < table.size(); ++code) {
        uint32_t grouped = static_cast<uint32_t>(code);
        for (uint32_t idx = 0; idx < 3; ++idx) {
            const uint32_t sample_code = grouped % static_cast<uint32_t>(quant.nlevels);
            grouped /= static_cast<uint32_t>(quant.nlevels);
            table[code][idx] = reconstruct_quantized_sample(quant, sample_code);
        }
    }
    return table;
}

inline constexpr std::array<AhxQuantClass, 17> AHX_QUANT_CLASSES = {{
    {3, 2, 5},
    {5, 4, 7},
    {7, 0, 3},
    {9, 4, 10},
    {15, 0, 4},
    {31, 0, 5},
    {63, 0, 6},
    {127, 0, 7},
    {255, 0, 8},
    {511, 0, 9},
    {1023, 0, 10},
    {2047, 0, 11},
    {4095, 0, 12},
    {8191, 0, 13},
    {16383, 0, 14},
    {32767, 0, 15},
    {65535, 0, 16},
}};

inline constexpr std::array<AhxQuantClass, 16> AHX_LOW_QUANT_CLASSES = {{
    AHX_QUANT_CLASSES[0],
    AHX_QUANT_CLASSES[1],
    AHX_QUANT_CLASSES[2],
    AHX_QUANT_CLASSES[3],
    AHX_QUANT_CLASSES[4],
    AHX_QUANT_CLASSES[5],
    AHX_QUANT_CLASSES[6],
    AHX_QUANT_CLASSES[7],
    AHX_QUANT_CLASSES[8],
    AHX_QUANT_CLASSES[9],
    AHX_QUANT_CLASSES[10],
    AHX_QUANT_CLASSES[11],
    AHX_QUANT_CLASSES[12],
    AHX_QUANT_CLASSES[13],
    AHX_QUANT_CLASSES[14],
    AHX_QUANT_CLASSES[15],
}};

inline constexpr std::array<AhxQuantClass, 16> AHX_HIGH_QUANT_CLASSES = {{
    AHX_QUANT_CLASSES[0],
    AHX_QUANT_CLASSES[1],
    AHX_QUANT_CLASSES[3],
    AHX_QUANT_CLASSES[4],
    AHX_QUANT_CLASSES[5],
    AHX_QUANT_CLASSES[6],
    AHX_QUANT_CLASSES[7],
    AHX_QUANT_CLASSES[8],
    AHX_QUANT_CLASSES[9],
    AHX_QUANT_CLASSES[10],
    AHX_QUANT_CLASSES[11],
    AHX_QUANT_CLASSES[12],
    AHX_QUANT_CLASSES[13],
    AHX_QUANT_CLASSES[14],
    AHX_QUANT_CLASSES[15],
    AHX_QUANT_CLASSES[16],
}};

inline constexpr auto AHX_BITALLOC_TABLE = generate_bitalloc_table();
inline constexpr auto AHX_OFFSET_TABLE = generate_offset_table();
inline constexpr auto AHX_QBITS_TABLE = generate_qbits_table(AHX_QUANT_CLASSES);
inline constexpr auto AHX_DECODE_QUANT_TABLE_LOW = generate_decode_quant_table(AHX_LOW_QUANT_CLASSES);
inline constexpr auto AHX_DECODE_QUANT_TABLE_HIGH = generate_decode_quant_table(AHX_HIGH_QUANT_CLASSES);
inline constexpr auto AHX_ENCODE_QUANT_TABLE_LOW = generate_encode_quant_table(AHX_DECODE_QUANT_TABLE_LOW);
inline constexpr auto AHX_ENCODE_QUANT_TABLE_HIGH = generate_encode_quant_table(AHX_DECODE_QUANT_TABLE_HIGH);
inline constexpr auto AHX_GROUPED_3_LEVEL_DECODE_TABLE =
    generate_grouped_decode_table<32>(AHX_DECODE_QUANT_TABLE_LOW[0]);
inline constexpr auto AHX_GROUPED_5_LEVEL_DECODE_TABLE =
    generate_grouped_decode_table<128>(AHX_DECODE_QUANT_TABLE_LOW[1]);
inline constexpr auto AHX_GROUPED_9_LEVEL_DECODE_TABLE =
    generate_grouped_decode_table<1024>(AHX_DECODE_QUANT_TABLE_LOW[3]);

inline constexpr std::array<int, 25> AHX_TRANSMISSION_PATTERN_TABLE = {
    291, 290, 290, 307, 291,
    275, 273, 273, 1092, 275,
    273, 273, 273, 819, 275,
    546, 546, 546, 819, 291,
    291, 290, 290, 307, 291,
};

} // namespace cricodecs::ahx::detail

#include "ahx_table_checks.hpp"
