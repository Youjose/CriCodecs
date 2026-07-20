#pragma once

#include "document/document_types.hpp"

#include "hca_codec.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace cristudio::modules::hca {

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview(
    const cricodecs::hca::Hca& hca,
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

} // namespace cristudio::modules::hca
