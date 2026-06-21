#pragma once
/**
 * @file hca_table_checks.hpp
 * @brief Compile-time reference checks for generated HCA tables.
 *
 * These references are not used at runtime; they document and verify generated
 * tables against values established from public references and CRI-binary
 * cross-checks during porting.
 */

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace cricodecs::hca::tables::reference {

consteval bool crc16_matches() {
    constexpr std::array<uint16_t, 256> reference = {
        0x0000,0x8005,0x800F,0x000A,0x801B,0x001E,0x0014,0x8011,0x8033,0x0036,0x003C,0x8039,0x0028,0x802D,0x8027,0x0022,
        0x8063,0x0066,0x006C,0x8069,0x0078,0x807D,0x8077,0x0072,0x0050,0x8055,0x805F,0x005A,0x804B,0x004E,0x0044,0x8041,
        0x80C3,0x00C6,0x00CC,0x80C9,0x00D8,0x80DD,0x80D7,0x00D2,0x00F0,0x80F5,0x80FF,0x00FA,0x80EB,0x00EE,0x00E4,0x80E1,
        0x00A0,0x80A5,0x80AF,0x00AA,0x80BB,0x00BE,0x00B4,0x80B1,0x8093,0x0096,0x009C,0x8099,0x0088,0x808D,0x8087,0x0082,
        0x8183,0x0186,0x018C,0x8189,0x0198,0x819D,0x8197,0x0192,0x01B0,0x81B5,0x81BF,0x01BA,0x81AB,0x01AE,0x01A4,0x81A1,
        0x01E0,0x81E5,0x81EF,0x01EA,0x81FB,0x01FE,0x01F4,0x81F1,0x81D3,0x01D6,0x01DC,0x81D9,0x01C8,0x81CD,0x81C7,0x01C2,
        0x0140,0x8145,0x814F,0x014A,0x815B,0x015E,0x0154,0x8151,0x8173,0x0176,0x017C,0x8179,0x0168,0x816D,0x8167,0x0162,
        0x8123,0x0126,0x012C,0x8129,0x0138,0x813D,0x8137,0x0132,0x0110,0x8115,0x811F,0x011A,0x810B,0x010E,0x0104,0x8101,
        0x8303,0x0306,0x030C,0x8309,0x0318,0x831D,0x8317,0x0312,0x0330,0x8335,0x833F,0x033A,0x832B,0x032E,0x0324,0x8321,
        0x0360,0x8365,0x836F,0x036A,0x837B,0x037E,0x0374,0x8371,0x8353,0x0356,0x035C,0x8359,0x0348,0x834D,0x8347,0x0342,
        0x03C0,0x83C5,0x83CF,0x03CA,0x83DB,0x03DE,0x03D4,0x83D1,0x83F3,0x03F6,0x03FC,0x83F9,0x03E8,0x83ED,0x83E7,0x03E2,
        0x83A3,0x03A6,0x03AC,0x83A9,0x03B8,0x83BD,0x83B7,0x03B2,0x0390,0x8395,0x839F,0x039A,0x838B,0x038E,0x0384,0x8381,
        0x0280,0x8285,0x828F,0x028A,0x829B,0x029E,0x0294,0x8291,0x82B3,0x02B6,0x02BC,0x82B9,0x02A8,0x82AD,0x82A7,0x02A2,
        0x82E3,0x02E6,0x02EC,0x82E9,0x02F8,0x82FD,0x82F7,0x02F2,0x02D0,0x82D5,0x82DF,0x02DA,0x82CB,0x02CE,0x02C4,0x82C1,
        0x8243,0x0246,0x024C,0x8249,0x0258,0x825D,0x8257,0x0252,0x0270,0x8275,0x827F,0x027A,0x826B,0x026E,0x0264,0x8261,
        0x0220,0x8225,0x822F,0x022A,0x823B,0x023E,0x0234,0x8231,0x8213,0x0216,0x021C,0x8219,0x0208,0x820D,0x8207,0x0202,
    };
    return HcaCrc16::table == reference;
}

consteval bool scale_to_resolution_curve_matches() {
    constexpr std::array<uint8_t, 59> reference = {
        15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12, 12,
        12, 12, 12, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10,
         9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  7,  6,  6,  5,
         4,  4,  4,  3,  3,  3,  2,  2,  2,  2,  1,
    };
    return SCALE_TO_RESOLUTION_CURVE == reference;
}

consteval bool resolution_invert_table_matches() {
    constexpr std::array<uint8_t, 66> reference = {
        14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12,
        12, 12, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10,  9,
         9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  7,  6,  6,  5,  4,
         4,  4,  3,  3,  3,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,
         1,  1,
    };
    return RESOLUTION_INVERT_TABLE == reference;
}

consteval bool quantized_spectrum_max_bits_match() {
    constexpr std::array<uint8_t, 16> reference = {
        0, 2, 3, 3, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    };
    return QUANTIZED_SPECTRUM_MAX_BITS == reference;
}

consteval bool scale_conversion_table_matches() {
    constexpr std::array<uint32_t, 128> reference = {
        0x00000000,0x32A0B051,0x32D61B5E,0x330EA43A,0x333E0F68,0x337D3E0C,0x33A8B6D5,0x33E0CCDF,
        0x3415C3FF,0x34478D75,0x3484F1F6,0x34B123F6,0x34EC0719,0x351D3EDA,0x355184DF,0x358B95C2,
        0x35B9FCD2,0x35F7D0DF,0x36251958,0x365BFBB8,0x36928E72,0x36C346CD,0x370218AF,0x372D583F,
        0x3766F85B,0x3799E046,0x37CD078C,0x3808980F,0x38360094,0x38728177,0x38A18FAF,0x38D744FD,
        0x390F6A81,0x393F179A,0x397E9E11,0x39A9A15B,0x39E2055B,0x3A16942D,0x3A48A2D8,0x3A85AAC3,
        0x3AB21A32,0x3AED4F30,0x3B1E196E,0x3B52A81E,0x3B8C57CA,0x3BBAFF5B,0x3BF9295A,0x3C25FED7,
        0x3C5D2D82,0x3C935A2B,0x3CC4563F,0x3D02CD87,0x3D2E4934,0x3D68396A,0x3D9AB62B,0x3DCE248C,
        0x3E0955EE,0x3E36FD92,0x3E73D290,0x3EA27043,0x3ED87039,0x3F1031DC,0x3F40213B,0x3F800000,
        0x3FAA8D26,0x3FE33F89,0x4017657D,0x4049B9BE,0x40866491,0x40B311C4,0x40EE9910,0x411EF532,
        0x4153CCF1,0x418D1ADF,0x41BC034A,0x41FA83B3,0x4226E595,0x425E60F5,0x429426FF,0x42C5672A,
        0x43038359,0x432F3B79,0x43697C38,0x439B8D3A,0x43CF4319,0x440A14D5,0x4437FBF0,0x4475257D,
        0x44A3520F,0x44D99D16,0x4510FA4D,0x45412C4D,0x4580B1ED,0x45AB7A3A,0x45E47B6D,0x461837F0,
        0x464AD226,0x46871F62,0x46B40AAF,0x46EFE4BA,0x471FD228,0x4754F35B,0x478DDF04,0x47BD08A4,
        0x47FBDFED,0x4827CD94,0x485F9613,0x4894F4F0,0x48C67991,0x49043A29,0x49302F0E,0x496AC0C7,
        0x499C6573,0x49D06334,0x4A0AD4C6,0x4A38FBAF,0x4A767A41,0x4AA43516,0x4ADACB94,0x4B11C3D3,
        0x4B4238D2,0x4B8164D2,0x4BAC6897,0x4BE5B907,0x4C190B88,0x4C4BEC15,0x00000000,0x00000000,
    };
    for (size_t i = 0; i < reference.size(); ++i) {
        if (std::bit_cast<uint32_t>(SCALE_CONVERSION_TABLE[i]) != reference[i]) {
            return false;
        }
    }
    return true;
}

consteval bool quantized_spectrum_decode_tables_match() {
    constexpr std::array<uint8_t, 128> read_bits = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 2, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 2, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4,
        3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    };
    constexpr std::array<int8_t, 128> values = {
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  1, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  1,  1, -1, -1,  2, -2,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  1, -1,  2, -2,  3, -3,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  1,  1, -1, -1,  2,  2, -2, -2,  3,  3, -3, -3,  4, -4,
         0,  0,  1,  1, -1, -1,  2,  2, -2, -2,  3, -3,  4, -4,  5, -5,
         0,  0,  1,  1, -1, -1,  2, -2,  3, -3,  4, -4,  5, -5,  6, -6,
         0,  0,  1, -1,  2, -2,  3, -3,  4, -4,  5, -5,  6, -6,  7, -7,
    };
    for (size_t i = 0; i < read_bits.size(); ++i) {
        if (QUANTIZED_SPECTRUM_DECODE_TABLES.read_bits[i] != read_bits[i]) {
            return false;
        }
        if (static_cast<int8_t>(QUANTIZED_SPECTRUM_DECODE_TABLES.values[i]) != values[i]) {
            return false;
        }
    }
    return true;
}

static_assert(
    crc16_matches(),
    "HCA CRC16 table generation no longer matches the reference table"
);
static_assert(
    scale_to_resolution_curve_matches(),
    "HCA scale-to-resolution curve generation no longer matches the reference table"
);
static_assert(
    resolution_invert_table_matches(),
    "HCA resolution invert table generation no longer matches the reference table"
);
static_assert(
    quantized_spectrum_max_bits_match(),
    "HCA quantized spectrum max-bits table generation no longer matches the reference table"
);
static_assert(
    scale_conversion_table_matches(),
    "HCA scale conversion table generation no longer matches the reference table"
);
static_assert(
    quantized_spectrum_decode_tables_match(),
    "HCA quantized spectrum decode table generation no longer matches the reference tables"
);

} // namespace cricodecs::hca::tables::reference
