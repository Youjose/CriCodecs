#pragma once

#include "document/document_types.hpp"

#include "aax_container.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace cristudio::modules::aax {

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview(
    const cricodecs::aax::AaxContainer& aax,
    const DecryptionKeys& keys
);

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
);

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview_from_file(
    const std::filesystem::path& path,
    const DecryptionKeys& keys
);

} // namespace cristudio::modules::aax
