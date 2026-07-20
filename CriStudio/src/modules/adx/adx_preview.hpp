#pragma once

#include "document/document_types.hpp"

#include "adx_codec.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::adx {

[[nodiscard]] std::vector<cricodecs::wav::SampleLoop> wav_loops_from_adx(
    const cricodecs::adx::AdxDecodeResult& decoded
);

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview(
    cricodecs::adx::Adx adx,
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

} // namespace cristudio::modules::adx
