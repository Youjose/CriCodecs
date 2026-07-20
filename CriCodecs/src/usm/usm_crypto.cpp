/**
 * @file usm_crypto.cpp
 * @brief USM stream masking.
 *
 * USM stream encryption is a lightweight chunk-local mask. Video and audio use
 * related masks derived from the container key, but the video direction is not
 * symmetric because later bytes feed the mask used for the first encrypted
 * window.
 */

#include "usm_crypto.hpp"

#include <algorithm>

namespace cricodecs::usm {

namespace {

constexpr size_t video_plain_prefix_size = 0x40;
constexpr size_t video_min_masked_size = 0x240;
constexpr size_t video_first_mask_words = 32;
constexpr size_t mask_size = 0x20;
constexpr size_t word_size = 8;

void mask_video_common(
    std::span<uint8_t> data,
    std::span<const uint8_t> mask1_bytes,
    std::span<const uint8_t> mask2_bytes,
    bool inverse
) {
    if (data.size() <= video_min_masked_size) {
        return;
    }

    auto* payload = data.data() + video_plain_prefix_size;
    const size_t payload_size = data.size() - video_plain_prefix_size;
    const size_t word_count = payload_size / word_size;
    if (word_count <= video_first_mask_words) {
        return;
    }
    const size_t masked_size = word_count * word_size;

    std::array<uint8_t, mask_size> mask1{};
    std::array<uint8_t, mask_size> mask2{};
    std::ranges::copy(mask1_bytes, mask1.begin());
    std::ranges::copy(mask2_bytes, mask2.begin());

    if (inverse) {
        for (size_t offset = 0; offset < video_first_mask_words * word_size; ++offset) {
            const size_t mask_offset = offset % mask_size;
            mask1[mask_offset] ^= payload[offset + video_first_mask_words * word_size];
            payload[offset] ^= mask1[mask_offset];
        }

        for (size_t offset = video_first_mask_words * word_size; offset < masked_size; ++offset) {
            const size_t mask_offset = offset % mask_size;
            const uint8_t plain = payload[offset];
            payload[offset] ^= mask2[mask_offset];
            mask2[mask_offset] = static_cast<uint8_t>(plain ^ mask2_bytes[mask_offset]);
        }
        return;
    }

    for (size_t offset = video_first_mask_words * word_size; offset < masked_size; ++offset) {
        const size_t mask_offset = offset % mask_size;
        payload[offset] ^= mask2[mask_offset];
        mask2[mask_offset] = static_cast<uint8_t>(payload[offset] ^ mask2_bytes[mask_offset]);
    }

    for (size_t offset = 0; offset < video_first_mask_words * word_size; ++offset) {
        const size_t mask_offset = offset % mask_size;
        mask1[mask_offset] ^= payload[offset + video_first_mask_words * word_size];
        payload[offset] ^= mask1[mask_offset];
    }

}

} // namespace

void UsmCrypto::init_key(uint64_t key) {
    m_key = key;
    m_has_key = true;

    const uint32_t lower = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    const uint32_t upper = static_cast<uint32_t>(key >> 32);

    const std::array<uint8_t, 4> key1 = {
        static_cast<uint8_t>((lower >> 24) & 0xFF),
        static_cast<uint8_t>((lower >> 16) & 0xFF),
        static_cast<uint8_t>((lower >> 8) & 0xFF),
        static_cast<uint8_t>(lower & 0xFF),
    };
    const std::array<uint8_t, 4> key2 = {
        static_cast<uint8_t>((upper >> 24) & 0xFF),
        static_cast<uint8_t>((upper >> 16) & 0xFF),
        static_cast<uint8_t>((upper >> 8) & 0xFF),
        static_cast<uint8_t>(upper & 0xFF),
    };

    std::array<uint8_t, 0x20> table{};
    table[0x00] = key1[3];
    table[0x01] = key1[2];
    table[0x02] = key1[1];
    table[0x03] = static_cast<uint8_t>((key1[0] - 0x34) & 0xFF);
    table[0x04] = static_cast<uint8_t>((key2[3] + 0xF9) & 0xFF);
    table[0x05] = static_cast<uint8_t>((key2[2] ^ 0x13) & 0xFF);
    table[0x06] = static_cast<uint8_t>((key2[1] + 0x61) & 0xFF);
    table[0x07] = static_cast<uint8_t>((key1[3] ^ 0xFF) & 0xFF);
    table[0x08] = static_cast<uint8_t>((key1[1] + key1[2]) & 0xFF);

    table[0x09] = static_cast<uint8_t>((table[0x01] - table[0x07]) & 0xFF);
    table[0x0A] = static_cast<uint8_t>((table[0x02] ^ 0xFF) & 0xFF);
    table[0x0B] = static_cast<uint8_t>((table[0x01] ^ 0xFF) & 0xFF);
    table[0x0C] = static_cast<uint8_t>((table[0x0B] + table[0x09]) & 0xFF);
    table[0x0D] = static_cast<uint8_t>((table[0x08] - table[0x03]) & 0xFF);
    table[0x0E] = static_cast<uint8_t>((table[0x0D] ^ 0xFF) & 0xFF);
    table[0x0F] = static_cast<uint8_t>((table[0x0A] - table[0x0B]) & 0xFF);
    table[0x10] = static_cast<uint8_t>((table[0x08] - table[0x0F]) & 0xFF);
    table[0x11] = static_cast<uint8_t>((table[0x10] ^ table[0x07]) & 0xFF);
    table[0x12] = static_cast<uint8_t>((table[0x0F] ^ 0xFF) & 0xFF);
    table[0x13] = static_cast<uint8_t>((table[0x03] ^ 0x10) & 0xFF);
    table[0x14] = static_cast<uint8_t>((table[0x04] - 0x32) & 0xFF);
    table[0x15] = static_cast<uint8_t>((table[0x05] + 0xED) & 0xFF);
    table[0x16] = static_cast<uint8_t>((table[0x06] ^ 0xF3) & 0xFF);
    table[0x17] = static_cast<uint8_t>((table[0x13] - table[0x0F]) & 0xFF);
    table[0x18] = static_cast<uint8_t>((table[0x15] + table[0x07]) & 0xFF);
    table[0x19] = static_cast<uint8_t>((0x21 - table[0x13]) & 0xFF);
    table[0x1A] = static_cast<uint8_t>((table[0x14] ^ table[0x17]) & 0xFF);
    table[0x1B] = static_cast<uint8_t>((table[0x16] + table[0x16]) & 0xFF);
    table[0x1C] = static_cast<uint8_t>((table[0x17] + 0x44) & 0xFF);
    table[0x1D] = static_cast<uint8_t>((table[0x03] + table[0x04]) & 0xFF);
    table[0x1E] = static_cast<uint8_t>((table[0x05] - table[0x16]) & 0xFF);
    table[0x1F] = static_cast<uint8_t>((table[0x1D] ^ table[0x13]) & 0xFF);

    m_video_mask1 = table;
    std::ranges::transform(table, m_video_mask2.begin(), [](uint8_t value) {
        return static_cast<uint8_t>(value ^ 0xFF);
    });

    static constexpr std::array<char, 4> audio_pattern = {'U', 'R', 'U', 'C'};
    for (size_t index = 0; index < m_audio_mask.size(); ++index) {
        if ((index & 1u) != 0) {
            m_audio_mask[index] = static_cast<uint8_t>(audio_pattern[(index >> 1) & 0x03]);
        } else {
            m_audio_mask[index] = m_video_mask2[index];
        }
    }
}

void UsmCrypto::clear_key() noexcept {
    m_key = 0;
    m_has_key = false;
    m_video_mask1 = {};
    m_video_mask2 = {};
    m_audio_mask = {};
}

void UsmCrypto::decrypt_video(std::span<uint8_t> data) const {
    if (!m_has_key) {
        return;
    }
    mask_video_common(data, m_video_mask1, m_video_mask2, false);
}

void UsmCrypto::encrypt_video(std::span<uint8_t> data) const {
    if (!m_has_key) {
        return;
    }
    mask_video_common(data, m_video_mask1, m_video_mask2, true);
}

void UsmCrypto::decrypt_audio(std::span<uint8_t> data) const {
    if (!m_has_key) {
        return;
    }

    constexpr size_t audio_plain_prefix_size = 0x140;

    if (data.size() <= audio_plain_prefix_size) {
        return;
    }

    auto* payload = data.data() + audio_plain_prefix_size;
    const size_t masked_size = ((data.size() - audio_plain_prefix_size) / word_size) * word_size;
    for (size_t offset = 0; offset < masked_size; ++offset) {
        payload[offset] ^= m_audio_mask[offset % mask_size];
    }
}

uint64_t UsmCrypto::recover_key_from_audio_mask(
    std::span<const uint8_t, 0x20> mask
) noexcept {
    const auto table = [&](size_t index) {
        return static_cast<uint8_t>(mask[index] ^ 0xFFu);
    };

    const uint8_t seed0 = table(0x00);
    const uint8_t seed2 = table(0x02);
    const uint8_t seed4 = table(0x04);
    const uint8_t seed6 = table(0x06);
    const uint8_t seed1 = static_cast<uint8_t>(table(0x08) - seed2);
    const uint8_t seed3 = static_cast<uint8_t>(
        table(0x08) - static_cast<uint8_t>(table(0x0E) ^ 0xFFu)
    );
    const uint8_t seed5 = static_cast<uint8_t>(table(0x1E) + table(0x16));

    const uint32_t lower =
        static_cast<uint32_t>(seed0) |
        (static_cast<uint32_t>(seed1) << 8u) |
        (static_cast<uint32_t>(seed2) << 16u) |
        (static_cast<uint32_t>(static_cast<uint8_t>(seed3 + 0x34u)) << 24u);
    const uint32_t upper =
        static_cast<uint32_t>(static_cast<uint8_t>(seed4 - 0xF9u)) |
        (static_cast<uint32_t>(seed5 ^ 0x13u) << 8u) |
        (static_cast<uint32_t>(static_cast<uint8_t>(seed6 - 0x61u)) << 16u);
    return static_cast<uint64_t>(lower) | (static_cast<uint64_t>(upper) << 32u);
}

} // namespace cricodecs::usm
