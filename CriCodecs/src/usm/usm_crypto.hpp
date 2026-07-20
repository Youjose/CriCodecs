#pragma once
/**
 * @file usm_crypto.hpp
 * @brief USM stream masking API.
 *
 * Mask behavior is grounded in PyCriCodecsEx handling and checked against the
 * Medianoche/SofDec 2 toolchain during the C++23 port. Public helpers by
 * Youjose.
 */

#include <array>
#include <cstdint>
#include <span>

namespace cricodecs::usm {

class UsmCrypto {
public:
    void init_key(uint64_t key);
    void clear_key() noexcept;

    void decrypt_video(std::span<uint8_t> data) const;
    void encrypt_video(std::span<uint8_t> data) const;
    void decrypt_audio(std::span<uint8_t> data) const;
    void encrypt_audio(std::span<uint8_t> data) const {
        decrypt_audio(data);
    }

    /// Return the initialized 32-byte USM audio mask.
    [[nodiscard]] const std::array<uint8_t, 0x20>& audio_mask() const noexcept {
        return m_audio_mask;
    }
    /// Invert the key-dependent even bytes of a complete USM audio mask.
    [[nodiscard]] static uint64_t recover_key_from_audio_mask(
        std::span<const uint8_t, 0x20> mask
    ) noexcept;

    [[nodiscard]] bool has_key() const noexcept { return m_has_key; }

private:
    uint64_t m_key = 0;
    bool m_has_key = false;
    std::array<uint8_t, 0x20> m_video_mask1{};
    std::array<uint8_t, 0x20> m_video_mask2{};
    std::array<uint8_t, 0x20> m_audio_mask{};
};

} // namespace cricodecs::usm
