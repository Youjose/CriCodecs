#pragma once
/**
 * @file text_encoding.hpp
 * @brief Legacy CRI string encoding helpers.
 *
 * Project-local conversion boundary for UTF-backed CRI metadata. Implemented
 * by Youjose.
 */

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::text {

struct EncodingOptions {
    std::optional<std::string> encoding;
};

[[nodiscard]] bool is_auto_encoding(const EncodingOptions& options) noexcept;
[[nodiscard]] std::string system_encoding();

[[nodiscard]] std::expected<std::string, std::string> decode_to_utf8(
    std::span<const uint8_t> bytes,
    const EncodingOptions& options = {}
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_from_utf8(
    std::string_view text,
    const EncodingOptions& options = {}
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_cri_string(
    std::string_view text,
    const EncodingOptions& options = {}
);

[[nodiscard]] bool contains_nul(std::span<const uint8_t> bytes) noexcept;
[[nodiscard]] bool contains_nul(std::string_view bytes) noexcept;

} // namespace cricodecs::text
