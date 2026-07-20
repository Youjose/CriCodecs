#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace cristudio {

struct VideoProbe {
    std::string suffix;
    std::string ffmpeg_input_format;
    std::string format;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
    uint32_t frame_count = 0;
    bool remux_for_playback = false;
};

[[nodiscard]] std::optional<VideoProbe> probe_video_bytes(std::span<const uint8_t> bytes);

[[nodiscard]] std::optional<std::string> usm_video_format_probe(std::span<const uint8_t> bytes);

[[nodiscard]] uint64_t duration_from_frames(
    uint32_t frames,
    uint32_t frame_rate_n,
    uint32_t frame_rate_d
);

} // namespace cristudio
