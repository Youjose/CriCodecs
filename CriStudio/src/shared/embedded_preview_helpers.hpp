#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace cristudio {

[[nodiscard]] std::string hex_dump(std::span<const uint8_t> bytes, size_t max_bytes, bool& truncated);
[[nodiscard]] bool likely_image_entry(std::string_view name, std::span<const uint8_t> bytes);

} // namespace cristudio
