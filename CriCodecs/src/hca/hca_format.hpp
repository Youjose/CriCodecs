#pragma once
/**
 * @file hca_format.hpp
 * @brief HCA format layout constants and version-specific layout predicates.
 */

#include <cstdint>

namespace cricodecs::hca {

inline constexpr int HCA_MASK             = 0x7F7F7F7F;
inline constexpr int HCA_SUBFRAMES        = 8;
inline constexpr int HCA_SAMPLES_PER_SUBFRAME = 128;
inline constexpr int HCA_SAMPLES_PER_FRAME    = HCA_SUBFRAMES * HCA_SAMPLES_PER_SUBFRAME; // 1024
inline constexpr int HCA_MDCT_BITS        = 7; // log2(128)
inline constexpr int HCA_MIN_FRAME_SIZE   = 8;
inline constexpr int HCA_MAX_FRAME_SIZE   = 0xFFFF;

inline constexpr uint16_t HCA_VERSION_V101 = 0x0101;
inline constexpr uint16_t HCA_VERSION_V102 = 0x0102;
inline constexpr uint16_t HCA_VERSION_V103 = 0x0103;
inline constexpr uint16_t HCA_VERSION_V200 = 0x0200;
inline constexpr uint16_t HCA_VERSION_V300 = 0x0300;

inline constexpr uint32_t HCA_CHUNK_ID_HCA  = 0x48434100; // "HCA\0"
inline constexpr uint32_t HCA_CHUNK_ID_FMT  = 0x666D7400; // "fmt\0"
inline constexpr uint32_t HCA_CHUNK_ID_COMP = 0x636F6D70; // "comp"
inline constexpr uint32_t HCA_CHUNK_ID_DEC  = 0x64656300; // "dec\0"
inline constexpr uint32_t HCA_CHUNK_ID_VBR  = 0x76627200; // "vbr\0"
inline constexpr uint32_t HCA_CHUNK_ID_ATH  = 0x61746800; // "ath\0"
inline constexpr uint32_t HCA_CHUNK_ID_LOOP = 0x6C6F6F70; // "loop"
inline constexpr uint32_t HCA_CHUNK_ID_CIPH = 0x63697068; // "ciph"
inline constexpr uint32_t HCA_CHUNK_ID_RVA  = 0x72766100; // "rva\0"
inline constexpr uint32_t HCA_CHUNK_ID_COMM = 0x636F6D6D; // "comm"
inline constexpr uint32_t HCA_CHUNK_ID_PAD  = 0x70616400; // "pad\0"

enum class ChannelType : uint8_t {
    Discrete = 0,
    StereoPrimary = 1,
    StereoSecondary = 2,
};

namespace detail {

[[nodiscard]] constexpr bool uses_dec_header(uint16_t version) noexcept {
    return version == HCA_VERSION_V102 || version == HCA_VERSION_V103;
}

[[nodiscard]] constexpr bool uses_comp_header(uint16_t version) noexcept {
    return version == HCA_VERSION_V200 || version == HCA_VERSION_V300;
}

[[nodiscard]] constexpr bool uses_v3_frame_layout(uint16_t version) noexcept {
    return version >= HCA_VERSION_V300;
}

[[nodiscard]] constexpr bool supports_encoder_version(uint16_t version) noexcept {
    return uses_dec_header(version) || uses_comp_header(version);
}

[[nodiscard]] constexpr bool writes_explicit_ath_chunk(uint16_t version) noexcept {
    return version == HCA_VERSION_V102 || version == HCA_VERSION_V103;
}

[[nodiscard]] constexpr uint16_t explicit_ath_type(uint16_t version, bool use_ath_curve) noexcept {
    if (writes_explicit_ath_chunk(version)) {
        return use_ath_curve ? 1u : 0u;
    }
    return 0u;
}

[[nodiscard]] constexpr bool default_ath_enabled_for_missing_chunk(uint16_t version) noexcept {
    return version < HCA_VERSION_V200;
}

} // namespace detail

} // namespace cricodecs::hca
