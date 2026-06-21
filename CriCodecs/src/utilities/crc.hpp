#pragma once
/**
 * @file crc.hpp
 * @brief Compile-time CRC helpers.
 *
 * Generic utility code for CriCodecs table generation and checks. Implemented
 * by Youjose.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cricodecs::util {

template <uint16_t Polynomial>
consteval std::array<uint16_t, 256> generate_crc16_msb_table() {
    std::array<uint16_t, 256> table{};
    for (int i = 0; i < static_cast<int>(table.size()); ++i) {
        uint16_t value = static_cast<uint16_t>(i << 8);
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 0x8000u) != 0
                ? static_cast<uint16_t>((value << 1) ^ Polynomial)
                : static_cast<uint16_t>(value << 1);
        }
        table[static_cast<size_t>(i)] = value;
    }
    return table;
}

template <uint16_t Polynomial>
struct Crc16Msb {
    inline static constexpr auto table = generate_crc16_msb_table<Polynomial>();

    [[nodiscard]] static constexpr uint16_t checksum(
        const uint8_t* data,
        size_t size,
        uint16_t seed = 0
    ) noexcept {
        uint16_t crc = seed;
        for (size_t i = 0; i < size; ++i) {
            crc = static_cast<uint16_t>((crc << 8) ^ table[((crc >> 8) ^ data[i]) & 0xFFu]);
        }
        return crc;
    }

    [[nodiscard]] static constexpr uint16_t checksum(std::span<const uint8_t> data, uint16_t seed = 0) noexcept {
        return checksum(data.data(), data.size(), seed);
    }
};

struct CriCrc32 {
    [[nodiscard]] static constexpr uint32_t update(
        const uint8_t* data,
        size_t size,
        uint32_t seed
    ) noexcept {
        uint32_t crc = seed;
        for (size_t i = 0; i < size; ++i) {
            crc = crc * 769u + data[i];
        }
        return crc;
    }

    [[nodiscard]] static constexpr uint32_t update(std::span<const uint8_t> data, uint32_t seed) noexcept {
        return update(data.data(), data.size(), seed);
    }

    [[nodiscard]] static constexpr uint32_t finalize(uint32_t crc) noexcept {
        return ~((crc != 0u) ? crc : 1u);
    }

    [[nodiscard]] static constexpr uint32_t checksum(
        const uint8_t* data,
        size_t size,
        uint32_t seed
    ) noexcept {
        return finalize(update(data, size, seed));
    }

    [[nodiscard]] static constexpr uint32_t checksum(std::span<const uint8_t> data, uint32_t seed) noexcept {
        return checksum(data.data(), data.size(), seed);
    }
};

} // namespace cricodecs::util
