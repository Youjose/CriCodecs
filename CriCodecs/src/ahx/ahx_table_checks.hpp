#pragma once
/**
 * @file ahx_table_checks.hpp
 * @brief Compile-time reference checks for generated AHX tables.
 *
 * These references are not used at runtime. They document and verify generated
 * tables against the fixed values carried by the earlier AHX port and the
 * symbolized CRI ahxencd binary. The goal is attribution and drift detection,
 * not additional runtime behavior.
 */

#include "ahx_allocation_tables.hpp"
#include "ahx_signal_tables.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cricodecs::ahx::detail::reference {

consteval bool bitalloc_table_matches() {
    constexpr std::array<uint8_t, 32> reference = {
        4, 4, 4, 4, 3, 3, 3, 3,
        3, 3, 3, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 0, 0,
    };
    return AHX_BITALLOC_TABLE == reference;
}

consteval bool offset_table_matches() {
    constexpr std::array<std::array<uint8_t, 16>, 5> reference = {{
        {{0}},
        {{0}},
        {{0, 1, 3, 4}},
        {{0, 1, 3, 4, 5, 6, 7, 8}},
        {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}},
    }};
    return AHX_OFFSET_TABLE == reference;
}

consteval bool qbits_table_matches() {
    constexpr std::array<int8_t, 17> reference = {
        -5, -7, 3, -10, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    };
    return AHX_QBITS_TABLE == reference;
}

template <size_t N>
consteval bool quant_table_matches(
    const std::array<AhxDecodeQuantSpec, N>& generated,
    const std::array<AhxDecodeQuantSpec, N>& reference)
{
    for (size_t i = 0; i < generated.size(); ++i) {
        if (generated[i].nlevels != reference[i].nlevels ||
            generated[i].group != reference[i].group ||
            generated[i].bits != reference[i].bits ||
            generated[i].c != reference[i].c ||
            generated[i].d != reference[i].d) {
            return false;
        }
    }
    return true;
}

consteval bool low_quant_table_matches() {
    constexpr std::array<AhxDecodeQuantSpec, 16> reference = {{
        {3, 2, 5, 0x15555555, 0x08000000},
        {5, 4, 7, 0x1999999a, 0x08000000},
        {7, 0, 3, 0x12492492, 0x04000000},
        {9, 4, 10, 0x1c71c71c, 0x08000000},
        {15, 0, 4, 0x11111111, 0x02000000},
        {31, 0, 5, 0x10842108, 0x01000000},
        {63, 0, 6, 0x10410410, 0x00800000},
        {127, 0, 7, 0x10204081, 0x00400000},
        {255, 0, 8, 0x10101010, 0x00200000},
        {511, 0, 9, 0x10080402, 0x00100000},
        {1023, 0, 10, 0x10040100, 0x00080000},
        {2047, 0, 11, 0x10020040, 0x00040000},
        {4095, 0, 12, 0x10010010, 0x00020000},
        {8191, 0, 13, 0x10008004, 0x00010000},
        {16383, 0, 14, 0x10004001, 0x00008000},
        {32767, 0, 15, 0x10002000, 0x00004000},
    }};
    return quant_table_matches(AHX_DECODE_QUANT_TABLE_LOW, reference);
}

consteval bool high_quant_table_matches() {
    constexpr std::array<AhxDecodeQuantSpec, 16> reference = {{
        {3, 2, 5, 0x15555555, 0x08000000},
        {5, 4, 7, 0x1999999a, 0x08000000},
        {9, 4, 10, 0x1c71c71c, 0x08000000},
        {15, 0, 4, 0x11111111, 0x02000000},
        {31, 0, 5, 0x10842108, 0x01000000},
        {63, 0, 6, 0x10410410, 0x00800000},
        {127, 0, 7, 0x10204081, 0x00400000},
        {255, 0, 8, 0x10101010, 0x00200000},
        {511, 0, 9, 0x10080402, 0x00100000},
        {1023, 0, 10, 0x10040100, 0x00080000},
        {2047, 0, 11, 0x10020040, 0x00040000},
        {4095, 0, 12, 0x10010010, 0x00020000},
        {8191, 0, 13, 0x10008004, 0x00010000},
        {16383, 0, 14, 0x10004001, 0x00008000},
        {32767, 0, 15, 0x10002000, 0x00004000},
        {65535, 0, 16, 0x10001000, 0x00002000},
    }};
    return quant_table_matches(AHX_DECODE_QUANT_TABLE_HIGH, reference);
}

consteval bool scalefactor_table_matches() {
    constexpr std::array<int64_t, 63> reference = {
        0x20000000, 0x1965fea5, 0x1428a2fa, 0x10000000, 0x0cb2ff53, 0x0a14517d, 0x08000000,
        0x06597fa9, 0x050a28be, 0x04000000, 0x032cbfd5, 0x0285145f, 0x02000000, 0x01965fea,
        0x01428a30, 0x01000000, 0x00cb2ff5, 0x00a14518, 0x00800000, 0x006597fb, 0x0050a28c,
        0x00400000, 0x0032cbfd, 0x00285146, 0x00200000, 0x001965ff, 0x001428a3, 0x00100000,
        0x000cb2ff, 0x000a1451, 0x00080000, 0x00065980, 0x00050a29, 0x00040000, 0x00032cc0,
        0x00028514, 0x00020000, 0x00019660, 0x0001428a, 0x00010000, 0x0000cb30, 0x0000a145,
        0x00008000, 0x00006598, 0x000050a3, 0x00004000, 0x000032cc, 0x00002851, 0x00002000,
        0x00001966, 0x00001429, 0x00001000, 0x00000cb3, 0x00000a14, 0x00000800, 0x00000659,
        0x0000050a, 0x00000400, 0x0000032d, 0x00000285, 0x00000200, 0x00000196, 0x00000143,
    };
    return AHX_SF_TABLE == reference;
}

consteval bool inverse_scalefactor_table_matches() {
    constexpr std::array<int64_t, 63> reference = {
        0x00000008000000, 0x0000000A14517C, 0x0000000CB2FF52, 0x00000010000000, 0x0000001428A2F8,
        0x0000001965FEA4, 0x00000020000000, 0x000000285145F5, 0x00000032CBFD4E, 0x00000040000000,
        0x00000050A28BDD, 0x0000006597FA9C, 0x00000080000000, 0x000000A14517ED, 0x000000CB2FF4E8,
        0x00000100000000, 0x000001428A2FDB, 0x000001965FE9D1, 0x00000200000000, 0x00000285145C8A,
        0x0000032CBFD3A3, 0x00000400000000, 0x0000050A28C5C7, 0x000006597FA747, 0x00000800000000,
        0x00000A145158C2, 0x00000CB2FF4E8E, 0x00001000000000, 0x00001428A37CB4, 0x00001965FFDFA8,
        0x00002000000000, 0x0000285143CCA8, 0x000032CBFAB527, 0x00004000000000, 0x000050A2879951,
        0x000065980992F3, 0x00008000000000, 0x0000A1450F32A2, 0x0000CB301325E7, 0x00010000000000,
        0x0001428A1E6544, 0x00019660264BCF, 0x00020000000000, 0x000285143CCA88, 0x00032CBB427564,
        0x00040000000000, 0x00050A28799510, 0x0006598AAD93B4, 0x00080000000000, 0x000A1450F32A20,
        0x000CB2C4B983B2, 0x00100000000000, 0x001428A1E65441, 0x001966CC01966C, 0x00200000000000,
        0x00285470CC2B7B, 0x0032CD98032CD9, 0x00400000000000, 0x00509C2E9A4AF1, 0x00659B300659B3,
        0x00800000000000, 0x00A16B312EA8FC, 0x00CAE5D85F1BBD,
    };
    return AHX_ISF_TABLE == reference;
}

static_assert(
    bitalloc_table_matches(),
    "AHX bit-allocation width table generation no longer matches the reference table"
);
static_assert(
    offset_table_matches(),
    "AHX allocation offset table generation no longer matches the reference table"
);
static_assert(
    qbits_table_matches(),
    "AHX quantized bit-count table generation no longer matches the reference table"
);
static_assert(
    low_quant_table_matches(),
    "AHX low-band quantization table generation no longer matches the reference table"
);
static_assert(
    high_quant_table_matches(),
    "AHX high-band quantization table generation no longer matches the reference table"
);
static_assert(
    scalefactor_table_matches(),
    "AHX scalefactor table generation no longer matches the reference table"
);
static_assert(
    inverse_scalefactor_table_matches(),
    "AHX inverse scalefactor table generation no longer matches the reference table"
);

} // namespace cricodecs::ahx::detail::reference
