#pragma once

#include "document/document_types.hpp"
#include "shared/document_extract_helpers.hpp"

#include <filesystem>

namespace cristudio::modules::acb {

void extract_cue_sheet_into_report(
    const LoadedDocument& document,
    const std::filesystem::path& output_dir,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    OutputPathAllocator& output_paths,
    ExtractionReport& report);

void extract_cue_target_into_report(
    const ExtractionTarget& target,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    OutputPathAllocator& output_paths,
    ExtractionReport& report);

} // namespace cristudio::modules::acb
