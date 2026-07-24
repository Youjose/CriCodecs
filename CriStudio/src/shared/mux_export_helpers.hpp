#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <filesystem>
#include <stop_token>
#include <string>

namespace cristudio {

[[nodiscard]] std::expected<void, std::string> write_mux_extract_file(
    const MuxPreview& mux,
    const std::filesystem::path& output_path,
    const std::filesystem::path& ffmpeg_path,
    std::stop_token stop_token = {}
);

} // namespace cristudio
