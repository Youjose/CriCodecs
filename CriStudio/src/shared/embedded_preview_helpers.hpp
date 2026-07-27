#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cristudio {

[[nodiscard]] std::string hex_dump(std::span<const uint8_t> bytes, size_t max_bytes, bool& truncated);
[[nodiscard]] bool is_supported_image_payload(std::span<const uint8_t> bytes);

} // namespace cristudio
