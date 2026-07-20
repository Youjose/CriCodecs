#pragma once

#include "document/document_types.hpp"

#include "usm_container.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace cristudio::modules::usm {

struct VideoMetadata {
    uint32_t total_frames = 0;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
};

using VideoFormatProbe = std::function<std::optional<std::string>(std::span<const uint8_t>)>;

[[nodiscard]] std::optional<VideoMetadata> video_metadata(
    const cricodecs::usm::UsmReader& usm,
    uint32_t stream_index
);

[[nodiscard]] std::optional<VideoMetadata> video_metadata_for_entry(const EntrySummary& entry);

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    cricodecs::usm::UsmReader& usm,
    const VideoFormatProbe& video_format_probe = {},
    bool inspect_stream_payloads = true
);

[[nodiscard]] LoadedDocument summarize_sbt_subtitles(
    const std::filesystem::path& path,
    std::span<const uint8_t> bytes,
    std::string& rejection_reason
);

} // namespace cristudio::modules::usm
