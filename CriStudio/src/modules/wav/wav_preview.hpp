#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace cristudio::modules::wav {

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& source_name
);

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview_from_file(
    const std::filesystem::path& path
);

} // namespace cristudio::modules::wav
