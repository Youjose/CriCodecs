#pragma once

#include "document/document_types.hpp"
#include "shared/document_extract_helpers.hpp"
#include "shared/embedded_entry_extractor.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace cristudio {

struct ExtractionContext {
    OutputPathAllocator output_paths;
    EmbeddedEntryExtractor embedded_entries;
};

void add_report_success(
    ExtractionReport& report,
    const std::filesystem::path& output_path,
    std::string_view label,
    const ExtractionOptions& options
);

void add_report_failure(
    ExtractionReport& report,
    std::string_view label,
    std::string_view error,
    const ExtractionOptions& options
);

void add_report_message(
    ExtractionReport& report,
    std::string message,
    const ExtractionOptions& options
);

} // namespace cristudio
