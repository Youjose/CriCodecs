#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace cristudio {

[[nodiscard]] std::optional<LoadedDocument> load_document_summary(
    const std::filesystem::path& path,
    std::string& rejection_reason,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::optional<LoadedDocument> probe_document_summary(
    const std::filesystem::path& path,
    std::string& rejection_reason
);

[[nodiscard]] std::optional<LoadedDocument> materialize_document_summary(
    const LoadedDocument& document,
    std::string& rejection_reason,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::optional<LoadedDocument> load_embedded_entry_summary(
    const EntrySummary& entry,
    std::string& rejection_reason,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> load_document_bytes(
    const LoadedDocument& document
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> load_embedded_entry_bytes(
    const EntrySummary& entry,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> raw_extract_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::expected<AudioPreview, std::string> build_audio_preview(
    const LoadedDocument& document,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] std::expected<MuxPreview, std::string> build_mux_preview(
    const LoadedDocument& document,
    int audio_choice,
    const DecryptionKeys& keys = {},
    std::stop_token stop_token = {}
);

[[nodiscard]] EmbeddedPreview load_embedded_entry_preview(
    const EntrySummary& entry,
    const DecryptionKeys& keys = {}
);

[[nodiscard]] ExtractionReport extract_targets(
    const std::vector<ExtractionTarget>& targets,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys = {},
    const ExtractionOptions& options = {}
);

} // namespace cristudio
