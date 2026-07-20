#pragma once

#include "document/document_types.hpp"

#include <optional>
#include <span>
#include <string>

namespace cristudio {

void attach_nested_sources(LoadedDocument& doc, const EntrySummary& outer_entry);
void finalize_embedded_document_summary(
    LoadedDocument& doc,
    const EntrySummary& entry,
    uint64_t byte_size
);

[[nodiscard]] std::optional<LoadedDocument> summarize_embedded_bytes(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    std::string& rejection_reason
);

} // namespace cristudio
