#pragma once

#include "document/document_types.hpp"

#include "awb_entry_codec.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>

namespace cristudio {

struct FfmpegMediaProbe {
    bool has_audio = false;
    bool has_video = false;
    std::string format;
};

[[nodiscard]] bool is_ffmpeg_audio_codec(cricodecs::awb::EntryCodec codec);

[[nodiscard]] std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_file(
    const std::filesystem::path& path,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_bytes(
    std::span<const uint8_t> bytes,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    cricodecs::awb::EntryCodec codec,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_file(
    const std::filesystem::path& path,
    std::stop_token stop_token = {}
);

} // namespace cristudio
