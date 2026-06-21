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
#include <vector>

namespace cricodecs::usm {

class UsmCrypto {
public:
    void init_key(uint64_t key);

    [[nodiscard]] std::vector<uint8_t> decrypt_video(std::span<const uint8_t> data) const;
    [[nodiscard]] std::vector<uint8_t> encrypt_video(std::span<const uint8_t> data) const;
    [[nodiscard]] std::vector<uint8_t> decrypt_audio(std::span<const uint8_t> data) const;
    [[nodiscard]] std::vector<uint8_t> encrypt_audio(std::span<const uint8_t> data) const {
        return decrypt_audio(data);
    }

    [[nodiscard]] bool has_key() const noexcept { return m_has_key; }

private:
    uint64_t m_key = 0;
    bool m_has_key = false;
    std::array<uint8_t, 0x20> m_video_mask1{};
    std::array<uint8_t, 0x20> m_video_mask2{};
    std::array<uint8_t, 0x20> m_audio_mask{};
};

} // namespace cricodecs::usm
