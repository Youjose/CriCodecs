#pragma once
/**
 * @file awb_aac_encryption.hpp
 * @brief CRI AWB AAC decryption helpers.
 *
 * Ported from vgmstream's awb_aac_encryption_streamfile.h and cross-checked
 * against cri_ware_unity's _criAacCodec_SetDecryptionKey,
 * _criAacCodec_DecryptData, and _criAacCodec_CheckDecryption.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cricodecs::awb {

enum class AacEncryptionState {
    Clear,
    Encrypted,
    Indeterminate,
};

[[nodiscard]] inline std::string_view to_string(AacEncryptionState state) noexcept {
    switch (state) {
        case AacEncryptionState::Clear:
            return "clear";
        case AacEncryptionState::Encrypted:
            return "encrypted";
        case AacEncryptionState::Indeterminate:
            return "indeterminate";
        default:
            return "unknown";
    }
}

namespace detail {

struct AacCipherState {
    std::array<uint16_t, 4> key{};
    uint16_t xor_value = 0;
    uint16_t add_value = 0;
    uint16_t mul_value = 0;
    uint32_t current_offset = 0;
};

[[nodiscard]] inline std::array<uint16_t, 4> derive_aac_key(uint64_t keycode) noexcept {
    if (keycode == 0) {
        return {};
    }

    const uint16_t k0 = static_cast<uint16_t>((4u * (keycode & 0x0FFFu)) | 1u);
    const uint16_t k1 = static_cast<uint16_t>((2u * ((keycode >> 12u) & 0x1FFFu)) | 1u);
    const uint16_t k2 = static_cast<uint16_t>((4u * ((keycode >> 25u) & 0x1FFFu)) | 1u);
    const uint16_t k3 = static_cast<uint16_t>((2u * ((keycode >> 38u) & 0x3FFFu)) | 1u);

    return {
        static_cast<uint16_t>(k0 ^ k1),
        static_cast<uint16_t>(k1 ^ k2),
        static_cast<uint16_t>(k2 ^ k3),
        static_cast<uint16_t>(~k3),
    };
}

inline void reset_aac_cipher(AacCipherState& state) noexcept {
    const uint16_t seed0 = static_cast<uint16_t>(~state.key[3]);
    const uint16_t seed1 = static_cast<uint16_t>(seed0 ^ state.key[2]);
    const uint16_t seed2 = static_cast<uint16_t>(seed1 ^ state.key[1]);

    state.xor_value = static_cast<uint16_t>((2u * seed0) | 1u);
    state.add_value = static_cast<uint16_t>((2u * seed0) | 1u);
    state.mul_value = static_cast<uint16_t>((4u * seed2) | 1u);
    state.current_offset = 0;
}

[[nodiscard]] inline AacCipherState make_aac_cipher_state(uint64_t keycode) noexcept {
    AacCipherState state;
    state.key = derive_aac_key(keycode);
    reset_aac_cipher(state);
    return state;
}

inline void apply_aac_keystream(std::span<uint8_t> data, AacCipherState& state) noexcept {
    if (data.empty()) {
        return;
    }

    const uint16_t seed0 = static_cast<uint16_t>(~state.key[3]);
    const uint16_t seed1 = static_cast<uint16_t>(seed0 ^ state.key[2]);
    const uint16_t seed2 = static_cast<uint16_t>(seed1 ^ state.key[1]);
    const uint16_t seed3 = static_cast<uint16_t>(seed2 ^ state.key[0]);

    uint16_t xor_value = state.xor_value;
    uint16_t add_value = state.add_value;
    uint16_t mul_value = state.mul_value;

    for (size_t i = 0; i < data.size(); ++i) {
        const uint32_t current_index = state.current_offset + static_cast<uint32_t>(i);
        if (static_cast<uint16_t>(current_index) == 0) {
            const uint32_t next_mul = (4u * seed2) + (seed3 * (mul_value & 0xFFFCu));
            const uint32_t next_add = (2u * seed0) + (seed1 * (add_value & 0xFFFEu));
            mul_value = static_cast<uint16_t>((next_mul & 0xFFFDu) | 1u);
            add_value = static_cast<uint16_t>(next_add | 1u);
        }

        xor_value = static_cast<uint16_t>((xor_value * mul_value) + add_value);
        data[i] ^= static_cast<uint8_t>(xor_value >> 8u);
    }

    state.xor_value = xor_value;
    state.add_value = add_value;
    state.mul_value = mul_value;
    state.current_offset += static_cast<uint32_t>(data.size());
}

} // namespace detail

inline void apply_aac_keystream(std::span<uint8_t> data, uint64_t keycode) noexcept {
    if (keycode == 0 || data.empty()) {
        return;
    }

    auto state = detail::make_aac_cipher_state(keycode);
    detail::apply_aac_keystream(data, state);
}

[[nodiscard]] inline std::vector<uint8_t> decrypt_aac(std::span<const uint8_t> data, uint64_t keycode) {
    std::vector<uint8_t> output(data.begin(), data.end());
    apply_aac_keystream(output, keycode);
    return output;
}

[[nodiscard]] inline AacEncryptionState probe_aac_encryption(std::span<const uint8_t> data,
                                                             uint64_t keycode) {
    constexpr std::array<uint8_t, 4> ftyp_magic{'f', 't', 'y', 'p'};

    if (data.size() < 8) {
        return AacEncryptionState::Indeterminate;
    }

    if (std::equal(ftyp_magic.begin(), ftyp_magic.end(), data.begin() + 4)) {
        return AacEncryptionState::Clear;
    }

    if (keycode == 0) {
        return AacEncryptionState::Indeterminate;
    }

    std::array<uint8_t, 8> probe{};
    std::copy_n(data.begin(), probe.size(), probe.begin());
    apply_aac_keystream(probe, keycode);

    if (std::equal(ftyp_magic.begin(), ftyp_magic.end(), probe.begin() + 4)) {
        return AacEncryptionState::Encrypted;
    }

    return AacEncryptionState::Indeterminate;
}

} // namespace cricodecs::awb
