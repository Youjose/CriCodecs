#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace cristudio::modules::usm {

enum class PreparedVideoFormat : uint8_t {
    Vp9Ivf,
    H264AnnexB,
    Mpeg1Elementary,
    Mpeg2Elementary,
};

struct PreparedVideoInfo {
    uint32_t frame_count = 0;
    uint64_t duration_ms = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PreparedAudioInfo {
    uint64_t sample_count = 0;
    uint64_t duration_ms = 0;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
};

[[nodiscard]] std::optional<PreparedVideoInfo> inspect_video_source_hint(
    const std::filesystem::path& path
);

[[nodiscard]] std::expected<PreparedVideoInfo, std::string> validate_prepared_video_output(
    const std::filesystem::path& path,
    PreparedVideoFormat expected_format,
    const std::optional<PreparedVideoInfo>& source_hint = std::nullopt
);

[[nodiscard]] std::expected<PreparedAudioInfo, std::string> validate_prepared_pcm_wav(
    const std::filesystem::path& path
);

} // namespace cristudio::modules::usm
