#include "shared/video_probe.hpp"

#include "h264.hpp"
#include "mpeg.hpp"

#include <algorithm>
#include <initializer_list>

namespace cristudio {
namespace {

uint16_t read_le16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
}

uint32_t read_le32(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
}

bool has_prefix(std::span<const uint8_t> bytes, std::initializer_list<uint8_t> prefix) {
    if (bytes.size() < prefix.size()) {
        return false;
    }
    return std::ranges::equal(prefix, bytes.first(prefix.size()));
}

bool has_h264_loader_prefix(std::span<const uint8_t> bytes) {
    return has_prefix(bytes, {0x00, 0x00, 0x00, 0x01, 0x09}) ||
           has_prefix(bytes, {0x00, 0x00, 0x00, 0x01, 0x67}) ||
           has_prefix(bytes, {0x00, 0x00, 0x01, 0x09}) ||
           has_prefix(bytes, {0x00, 0x00, 0x01, 0x67});
}

std::optional<VideoProbe> probe_ivf_video(std::span<const uint8_t> bytes) {
    if (!has_prefix(bytes, {'D', 'K', 'I', 'F'}) || bytes.size() < 32) {
        return std::nullopt;
    }

    const auto header_size = static_cast<size_t>(read_le16(bytes, 6));
    if (header_size < 32 || header_size > bytes.size()) {
        return std::nullopt;
    }

    size_t offset = header_size;
    uint32_t frame_count = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 12) {
            return std::nullopt;
        }
        const auto frame_size = static_cast<size_t>(read_le32(bytes, offset));
        offset += 12;
        if (frame_size > bytes.size() - offset) {
            return std::nullopt;
        }
        offset += frame_size;
        ++frame_count;
    }

    if (frame_count == 0) {
        return std::nullopt;
    }

    const std::string fourcc(reinterpret_cast<const char*>(bytes.data() + 8), 4);
    if (fourcc == "VP90") {
        return VideoProbe{.suffix = ".ivf", .ffmpeg_input_format = "ivf", .format = "VP9/IVF video", .frame_count = frame_count};
    }
    if (fourcc == "VP80") {
        return VideoProbe{.suffix = ".ivf", .ffmpeg_input_format = "ivf", .format = "VP8/IVF video", .frame_count = frame_count};
    }
    if (fourcc == "AV01") {
        return VideoProbe{.suffix = ".ivf", .ffmpeg_input_format = "ivf", .format = "AV1/IVF video", .frame_count = frame_count};
    }
    return VideoProbe{.suffix = ".ivf", .ffmpeg_input_format = "ivf", .format = "IVF video", .frame_count = frame_count};
}

std::optional<VideoProbe> probe_h264_video(std::span<const uint8_t> bytes) {
    if (!has_h264_loader_prefix(bytes)) {
        return std::nullopt;
    }

    cricodecs::video::H264VideoReader reader;
    if (!reader.open(bytes)) {
        return std::nullopt;
    }
    const auto [fps_n, fps_d] = reader.frame_rate();
    return VideoProbe{
        .suffix = ".h264",
        .ffmpeg_input_format = "h264",
        .format = "H.264 video",
        .frame_rate_n = fps_n,
        .frame_rate_d = fps_d,
        .frame_count = reader.frame_count(),
        .remux_for_playback = true,
    };
}

std::optional<VideoProbe> probe_mpeg_video(std::span<const uint8_t> bytes) {
    cricodecs::video::MpegVideoReader reader;
    if (!reader.open(bytes)) {
        return std::nullopt;
    }

    const auto suffix = reader.video_type() == cricodecs::video::MpegVideoType::mpeg1 ? ".m1v" : ".m2v";
    const auto format = reader.video_type() == cricodecs::video::MpegVideoType::mpeg1 ? "MPEG-1 video"
                      : reader.video_type() == cricodecs::video::MpegVideoType::mpeg2 ? "MPEG-2 video"
                      : "MPEG video";
    const auto [fps_n, fps_d] = reader.frame_rate();
    return VideoProbe{
        .suffix = suffix,
        .ffmpeg_input_format = "mpegvideo",
        .format = format,
        .frame_rate_n = fps_n,
        .frame_rate_d = fps_d,
        .frame_count = reader.frame_count(),
        .remux_for_playback = true,
    };
}

} // namespace

std::optional<VideoProbe> probe_video_bytes(std::span<const uint8_t> bytes) {
    if (auto video = probe_ivf_video(bytes)) {
        return video;
    }
    if (auto video = probe_h264_video(bytes)) {
        return video;
    }
    return probe_mpeg_video(bytes);
}

std::optional<std::string> usm_video_format_probe(std::span<const uint8_t> bytes) {
    if (auto video = probe_video_bytes(bytes)) {
        return video->format;
    }
    return std::nullopt;
}

uint64_t duration_from_frames(uint32_t frames, uint32_t frame_rate_n, uint32_t frame_rate_d) {
    if (frames == 0 || frame_rate_n == 0 || frame_rate_d == 0) {
        return 0;
    }
    return (static_cast<uint64_t>(frames) * 1000ull * static_cast<uint64_t>(frame_rate_d)) /
           static_cast<uint64_t>(frame_rate_n);
}

} // namespace cristudio
