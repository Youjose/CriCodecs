#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cristudio {

[[nodiscard]] bool is_audio_format(std::string_view format);
[[nodiscard]] bool is_direct_audio_format(std::string_view format);
[[nodiscard]] bool is_mux_document_format(std::string_view format);
[[nodiscard]] bool is_audio_entry(const EntrySummary& entry);
[[nodiscard]] bool is_sbt_entry(const EntrySummary& entry);
[[nodiscard]] bool supports_editor(const LoadedDocument& document);
[[nodiscard]] bool supports_editor(const EntrySummary& entry);

[[nodiscard]] std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    const LoadedDocument& document,
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
);
[[nodiscard]] std::optional<VideoPreview> video_preview_from_bytes(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes
);
[[nodiscard]] std::expected<AudioPreview, std::string> build_direct_audio_preview(
    const LoadedDocument& document,
    const DecryptionKeys& keys
);

} // namespace cristudio
