#pragma once

#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include "sfd_container.hpp"
#include "usm_container.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cristudio::modules::usm {

enum class MediaBuildTarget : uint8_t {
    Usm,
    Sfd,
};

enum class MediaVideoPrep : uint8_t {
    UsePrepared = 0,
    FfmpegVp9,
    FfmpegH264,
    FfmpegMpeg1,
    FfmpegMpeg2,
};

enum class MediaAudioPrep : uint8_t {
    UsePrepared = 0,
    FfmpegToAdx,
    FfmpegToHca,
};

enum class MediaCodecPreset : uint8_t {
    Compact = 0,
    Standard,
    HighQuality,
};

struct MediaBuildConfig {
    MediaBuildTarget target = MediaBuildTarget::Usm;
    MediaVideoPrep video_prep = MediaVideoPrep::UsePrepared;
    MediaCodecPreset video_preset = MediaCodecPreset::Standard;
    cricodecs::sfd::SfdBuildProfile sfd_profile = cricodecs::sfd::SfdBuildProfile::sofdec_stream_standard_fixed_2048;
    std::filesystem::path video_source;
    std::filesystem::path output_path;
    std::filesystem::path ffmpeg_path;
    DecryptionKeys keys;
    bool apply_to_editor = false;
    bool encrypt_audio = false;

    struct AudioTrack {
        std::filesystem::path source;
        MediaAudioPrep prep = MediaAudioPrep::UsePrepared;
        std::optional<uint8_t> channel_no = std::nullopt;
    };
    std::vector<AudioTrack> audio_tracks;

    struct SubtitleTrack {
        std::filesystem::path source;
        cricodecs::usm::UsmSubtitleFormat format = cricodecs::usm::UsmSubtitleFormat::Auto;
        uint32_t language_id = 0;
        std::optional<uint8_t> channel_no = std::nullopt;
    };
    std::vector<SubtitleTrack> subtitle_tracks;

    struct ExistingUsmTrack {
        enum class Kind : uint8_t {
            Video,
            Audio,
            Subtitle,
            Unsupported,
        };

        Kind kind = Kind::Video;
        uint32_t stream_index = 0;
        uint8_t channel = 0;
        std::optional<cricodecs::usm::UsmAudioCodec> audio_codec = std::nullopt;
        std::string label;
        std::filesystem::path replacement_source;
        MediaVideoPrep video_prep = MediaVideoPrep::UsePrepared;
        MediaAudioPrep audio_prep = MediaAudioPrep::UsePrepared;
        cricodecs::usm::UsmSubtitleFormat subtitle_format = cricodecs::usm::UsmSubtitleFormat::Auto;
        uint32_t subtitle_language_id = 0;
        bool enabled = true;
    };

    std::filesystem::path existing_usm_path;
    bool has_existing_usm_bytes = false;
    std::vector<uint8_t> existing_usm_bytes;
    std::vector<ExistingUsmTrack> existing_usm_tracks;
};

using MediaBuildLogCallback = std::function<void(QString)>;

[[nodiscard]] std::optional<QString> validate_media_build_config(
    const MediaBuildConfig& config,
    bool inspect_sources
);

[[nodiscard]] std::expected<void, QString> build_media_from_sources(
    MediaBuildConfig config,
    MediaBuildLogCallback log
);

[[nodiscard]] std::vector<TransformDetailRow> media_build_job_detail_rows(const DecryptionKeys& keys);

} // namespace cristudio::modules::usm
