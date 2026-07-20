#pragma once

#include "document/document_types.hpp"
#include "shared/document_extraction_report.hpp"

#include <filesystem>
#include <span>

namespace cristudio::modules::usm {

[[nodiscard]] bool extract_sbt_sidecars(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir,
    ExtractionContext& context,
    ExtractionReport& report,
    const ExtractionOptions& options
);

} // namespace cristudio::modules::usm
