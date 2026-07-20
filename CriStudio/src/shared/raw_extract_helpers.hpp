#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> raw_extract_transform(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
);

} // namespace cristudio
