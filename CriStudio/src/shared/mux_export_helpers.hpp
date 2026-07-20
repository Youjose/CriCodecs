#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace cristudio {

[[nodiscard]] std::expected<void, std::string> write_mux_extract_file(
    const MuxPreview& mux,
    const std::filesystem::path& output_path,
    const std::filesystem::path& ffmpeg_path
);

} // namespace cristudio
