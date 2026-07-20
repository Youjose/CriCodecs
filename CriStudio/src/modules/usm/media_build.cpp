#include "modules/usm/media_build.hpp"
#include "modules/usm/media_build_validation.hpp"

#include "path_text.hpp"
#include "shared/document_extract_helpers.hpp"

#include "adx_codec.hpp"
#include "hca_codec.hpp"
#include "usm_container.hpp"
#include "video/mpeg.hpp"
#include "wav_container.hpp"

#include <QLockFile>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>

#include <array>
#include <optional>
#include <system_error>

namespace cristudio::modules::usm {
namespace {

void push_log(const MediaBuildLogCallback& log, QString message) {
    if (log) {
        log(std::move(message));
    }
}

QString quoted_arg(const QString& text) {
    auto escaped = text;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString command_line_text(const std::filesystem::path& executable, const QStringList& arguments) {
    QStringList parts = {quoted_arg(path_to_qstring(executable))};
    for (const auto& argument : arguments) {
        parts.push_back(quoted_arg(argument));
    }
    return parts.join(QLatin1Char(' '));
}

std::expected<void, QString> run_process_logged(
    const std::filesystem::path& executable,
    const QStringList& arguments,
    const QString& label,
    const MediaBuildLogCallback& log
) {
    push_log(log, QStringLiteral("%1: %2").arg(label).arg(command_line_text(executable, arguments)));

    QProcess process;
    process.setProgram(path_to_qstring(executable));
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        return std::unexpected(QStringLiteral("%1 failed: could not start process").arg(label));
    }

    constexpr qint64 process_timeout_ms = 6LL * 60LL * 60LL * 1000LL;
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(250);
        const auto output_chunk = QString::fromLocal8Bit(process.readAll()).trimmed();
        if (!output_chunk.isEmpty()) {
            push_log(log, output_chunk);
        }
        if (process.state() != QProcess::NotRunning && elapsed.hasExpired(process_timeout_ms)) {
            process.terminate();
            if (!process.waitForFinished(5000)) {
                process.kill();
                process.waitForFinished(5000);
            }
            const auto output_tail = QString::fromLocal8Bit(process.readAll()).trimmed();
            if (!output_tail.isEmpty()) {
                push_log(log, output_tail);
            }
            return std::unexpected(QStringLiteral("%1 timed out after 6 hours").arg(label));
        }
    }

    const auto output_tail = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (!output_tail.isEmpty()) {
        push_log(log, output_tail);
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::unexpected(QStringLiteral("%1 failed with exit code %2").arg(label).arg(process.exitCode()));
    }
    push_log(log, QStringLiteral("%1 finished.").arg(label));
    return {};
}

std::expected<void, QString> publish_staged_file(
    const std::filesystem::path& staged_path,
    const std::filesystem::path& output_path
) {
    QFile input(path_to_qstring(staged_path));
    if (!input.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("Could not open staged output: %1").arg(input.errorString()));
    }

    QSaveFile output(path_to_qstring(output_path));
    if (!output.open(QIODevice::WriteOnly)) {
        return std::unexpected(QStringLiteral("Could not open output transaction: %1").arg(output.errorString()));
    }

    constexpr qint64 copy_chunk_size = 1024 * 1024;
    while (!input.atEnd()) {
        const auto bytes = input.read(copy_chunk_size);
        if (bytes.isEmpty() && input.error() != QFileDevice::NoError) {
            return std::unexpected(QStringLiteral("Could not read staged output: %1").arg(input.errorString()));
        }
        if (bytes.isEmpty()) {
            break;
        }
        if (output.write(bytes) != bytes.size()) {
            return std::unexpected(QStringLiteral("Could not write output transaction: %1").arg(output.errorString()));
        }
    }
    if (!output.commit()) {
        return std::unexpected(QStringLiteral("Could not commit output transaction: %1").arg(output.errorString()));
    }
    return {};
}

std::expected<std::filesystem::path, QString> prepare_video_source(
    const std::filesystem::path& source,
    MediaVideoPrep prep,
    MediaCodecPreset preset,
    const std::filesystem::path& ffmpeg_path,
    const std::filesystem::path& stage_dir,
    const MediaBuildLogCallback& log
) {
    if (source.empty()) {
        return std::unexpected(QStringLiteral("Video input is required"));
    }
    if (!std::filesystem::exists(source)) {
        return std::unexpected(QStringLiteral("Video input does not exist: %1").arg(path_to_qstring(source)));
    }
    if (prep == MediaVideoPrep::UsePrepared) {
        push_log(log, QStringLiteral("Using prepared video source: %1").arg(path_to_qstring(source)));
        return source;
    }
    if (ffmpeg_path.empty()) {
        return std::unexpected(QStringLiteral("FFmpeg is required for selected video preparation"));
    }

    const auto source_hint = inspect_video_source_hint(source);
    std::filesystem::path output;
    std::optional<PreparedVideoFormat> expected_format;
    QStringList arguments = {
        QStringLiteral("-y"),
        QStringLiteral("-xerror"),
        QStringLiteral("-err_detect"),
        QStringLiteral("explode"),
        QStringLiteral("-i"),
        path_to_qstring(source),
        QStringLiteral("-an")
    };
    switch (prep) {
    case MediaVideoPrep::FfmpegVp9:
        output = stage_dir / "video.ivf";
        expected_format = PreparedVideoFormat::Vp9Ivf;
        arguments << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                  << QStringLiteral("-crf") << (preset == MediaCodecPreset::Compact
                      ? QStringLiteral("36")
                      : (preset == MediaCodecPreset::HighQuality ? QStringLiteral("22") : QStringLiteral("30")))
                  << QStringLiteral("-b:v") << QStringLiteral("0")
                  << QStringLiteral("-deadline") << (preset == MediaCodecPreset::HighQuality
                      ? QStringLiteral("good")
                      : QStringLiteral("realtime"))
                  << QStringLiteral("-cpu-used") << (preset == MediaCodecPreset::HighQuality
                      ? QStringLiteral("2")
                      : (preset == MediaCodecPreset::Compact ? QStringLiteral("6") : QStringLiteral("4")))
                  << QStringLiteral("-f") << QStringLiteral("ivf")
                  << path_to_qstring(output);
        break;
    case MediaVideoPrep::FfmpegH264:
        output = stage_dir / "video.264";
        expected_format = PreparedVideoFormat::H264AnnexB;
        arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                  << QStringLiteral("-preset") << (preset == MediaCodecPreset::HighQuality ? QStringLiteral("slow") : QStringLiteral("medium"))
                  << QStringLiteral("-crf") << (preset == MediaCodecPreset::Compact
                      ? QStringLiteral("28")
                      : (preset == MediaCodecPreset::HighQuality ? QStringLiteral("18") : QStringLiteral("23")))
                  << QStringLiteral("-f") << QStringLiteral("h264")
                  << path_to_qstring(output);
        break;
    case MediaVideoPrep::FfmpegMpeg1:
        output = stage_dir / "video.m1v";
        expected_format = PreparedVideoFormat::Mpeg1Elementary;
        arguments << QStringLiteral("-c:v") << QStringLiteral("mpeg1video")
                  << QStringLiteral("-b:v") << (preset == MediaCodecPreset::Compact
                      ? QStringLiteral("2000k")
                      : (preset == MediaCodecPreset::HighQuality ? QStringLiteral("9000k") : QStringLiteral("6000k")))
                  << QStringLiteral("-f") << QStringLiteral("mpeg1video")
                  << path_to_qstring(output);
        break;
    case MediaVideoPrep::FfmpegMpeg2:
        output = stage_dir / "video.m2v";
        expected_format = PreparedVideoFormat::Mpeg2Elementary;
        arguments << QStringLiteral("-c:v") << QStringLiteral("mpeg2video")
                  << QStringLiteral("-b:v") << (preset == MediaCodecPreset::Compact
                      ? QStringLiteral("2500k")
                      : (preset == MediaCodecPreset::HighQuality ? QStringLiteral("10000k") : QStringLiteral("7000k")))
                  << QStringLiteral("-f") << QStringLiteral("mpeg2video")
                  << path_to_qstring(output);
        break;
    case MediaVideoPrep::UsePrepared:
        break;
    }

    if (auto result = run_process_logged(ffmpeg_path, arguments, QStringLiteral("FFmpeg video prepare"), log); !result) {
        return std::unexpected(result.error());
    }
    if (!expected_format) {
        return std::unexpected(QStringLiteral("FFmpeg video output validation has no expected format"));
    }
    auto validated = validate_prepared_video_output(output, *expected_format, source_hint);
    if (!validated) {
        return std::unexpected(utf8_to_qstring(validated.error()));
    }
    push_log(log, QStringLiteral("Validated FFmpeg video output: %1 frames, %2 ms, %3x%4.")
        .arg(validated->frame_count)
        .arg(validated->duration_ms)
        .arg(validated->width)
        .arg(validated->height));
    return output;
}

std::expected<std::optional<std::filesystem::path>, QString> prepare_audio_source(
    const std::filesystem::path& source,
    MediaAudioPrep prep,
    const std::filesystem::path& ffmpeg_path,
    const std::filesystem::path& stage_dir,
    std::string_view stage_name,
    const MediaBuildLogCallback& log
) {
    if (source.empty()) {
        return std::unexpected(QStringLiteral("Audio input is required for selected audio preparation"));
    }
    if (!std::filesystem::exists(source)) {
        return std::unexpected(QStringLiteral("Audio input does not exist: %1").arg(path_to_qstring(source)));
    }
    if (prep == MediaAudioPrep::UsePrepared) {
        push_log(log, QStringLiteral("Using prepared ADX/HCA audio source: %1").arg(path_to_qstring(source)));
        return std::optional<std::filesystem::path>{source};
    }
    if (ffmpeg_path.empty()) {
        return std::unexpected(QStringLiteral("FFmpeg is required for selected audio preparation"));
    }

    const auto wav_path = stage_dir / (std::string(stage_name) + ".wav");
    const bool hca_output = prep == MediaAudioPrep::FfmpegToHca;
    const auto encoded_path = stage_dir / (std::string(stage_name) + (hca_output ? ".hca" : ".adx"));
    QStringList arguments = {
        QStringLiteral("-y"),
        QStringLiteral("-xerror"),
        QStringLiteral("-err_detect"),
        QStringLiteral("explode"),
        QStringLiteral("-i"),
        path_to_qstring(source),
        QStringLiteral("-vn"),
        QStringLiteral("-acodec"),
        QStringLiteral("pcm_s16le"),
        QStringLiteral("-f"),
        QStringLiteral("wav"),
        path_to_qstring(wav_path)
    };
    if (auto result = run_process_logged(ffmpeg_path, arguments, QStringLiteral("FFmpeg audio prepare"), log); !result) {
        return std::unexpected(result.error());
    }

    auto validated = validate_prepared_pcm_wav(wav_path);
    if (!validated) {
        return std::unexpected(utf8_to_qstring(validated.error()));
    }
    push_log(log, QStringLiteral("Validated FFmpeg audio output: %1 sample frames, %2 ms, %3 Hz, %4 channel(s).")
        .arg(validated->sample_count)
        .arg(validated->duration_ms)
        .arg(validated->sample_rate)
        .arg(validated->channels));

    cricodecs::wav::WavContainer wav;
    if (auto result = wav.load(wav_path); !result) {
        return std::unexpected(QStringLiteral("Prepared WAV load failed: %1").arg(utf8_to_qstring(result.error())));
    }
    std::expected<std::vector<uint8_t>, std::string> encoded;
    if (hca_output) {
        cricodecs::hca::HcaEncodeConfig hca_config;
        encoded = cricodecs::hca::encode(wav, hca_config);
    } else {
        cricodecs::adx::AdxEncodeConfig adx_config;
        encoded = cricodecs::adx::AdxEncoder::encode(wav, adx_config);
    }
    if (!encoded) {
        return std::unexpected(QStringLiteral("%1 encode failed: %2")
            .arg(hca_output ? QStringLiteral("HCA") : QStringLiteral("ADX"))
            .arg(utf8_to_qstring(encoded.error())));
    }
    if (auto result = write_binary_file(encoded_path, *encoded); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    push_log(log, QStringLiteral("Encoded %1 audio: %2")
        .arg(hca_output ? QStringLiteral("HCA") : QStringLiteral("ADX"))
        .arg(path_to_qstring(encoded_path)));
    return std::optional<std::filesystem::path>{encoded_path};
}

bool is_existing_usm_build(const MediaBuildConfig& config) {
    return !config.existing_usm_tracks.empty();
}

std::expected<cricodecs::usm::UsmReader, QString> load_existing_usm(MediaBuildConfig& config) {
    cricodecs::usm::UsmReader reader;
    if (!config.existing_usm_path.empty()) {
        if (auto result = reader.load(config.existing_usm_path); !result) {
            return std::unexpected(QStringLiteral("Current USM load failed: %1").arg(utf8_to_qstring(result.error())));
        }
    } else {
        if (config.existing_usm_bytes.empty()) {
            return std::unexpected(QStringLiteral("Current USM source bytes are not available"));
        }
        if (auto result = reader.load(std::move(config.existing_usm_bytes)); !result) {
            return std::unexpected(QStringLiteral("Current USM load failed: %1").arg(utf8_to_qstring(result.error())));
        }
    }
    if (config.keys.has_cri_key) {
        reader.set_key(config.keys.cri_key);
    }
    return reader;
}

std::filesystem::path existing_track_path(
    const std::filesystem::path& stage_dir,
    const MediaBuildConfig::ExistingUsmTrack& track,
    bool hca_audio = false
) {
    std::string extension = ".bin";
    switch (track.kind) {
    case MediaBuildConfig::ExistingUsmTrack::Kind::Video:
        extension = ".video";
        break;
    case MediaBuildConfig::ExistingUsmTrack::Kind::Audio:
        extension = hca_audio ? ".hca" : ".adx";
        break;
    case MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle:
        extension = ".sbt";
        break;
    case MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported:
        break;
    }
    return stage_dir / ("existing_stream_" + std::to_string(track.stream_index) + extension);
}

std::expected<std::filesystem::path, QString> materialize_existing_track(
    cricodecs::usm::UsmReader& reader,
    const MediaBuildConfig::ExistingUsmTrack& track,
    const std::filesystem::path& stage_dir,
    const MediaBuildLogCallback& log
) {
    if (track.kind == MediaBuildConfig::ExistingUsmTrack::Kind::Audio) {
        auto bytes = reader.extract_stream(track.stream_index);
        if (!bytes) {
            return std::unexpected(QStringLiteral("USM stream %1 extract failed: %2")
                .arg(track.stream_index)
                .arg(utf8_to_qstring(bytes.error())));
        }
        const bool is_hca = track.audio_codec == cricodecs::usm::UsmAudioCodec::Hca;
        const auto output_path = existing_track_path(stage_dir, track, is_hca);
        if (auto written = write_binary_file(output_path, *bytes); !written) {
            return std::unexpected(utf8_to_qstring(written.error()));
        }
        push_log(log, QStringLiteral("Using current USM stream %1: %2")
            .arg(track.stream_index)
            .arg(path_to_qstring(output_path)));
        return output_path;
    }

    const auto output_path = existing_track_path(stage_dir, track);
    auto extracted = reader.extract_file(track.stream_index, output_path);
    if (!extracted) {
        return std::unexpected(QStringLiteral("USM stream %1 extract failed: %2")
            .arg(track.stream_index)
            .arg(utf8_to_qstring(extracted.error())));
    }
    push_log(log, QStringLiteral("Using current USM stream %1: %2")
        .arg(track.stream_index)
        .arg(path_to_qstring(output_path)));
    return output_path;
}

std::expected<void, QString> build_existing_usm_from_tracks(
    MediaBuildConfig& config,
    const std::filesystem::path& stage_dir,
    const std::filesystem::path& output_path,
    const MediaBuildLogCallback& log
) {
    auto reader = load_existing_usm(config);
    if (!reader) {
        return std::unexpected(reader.error());
    }

    cricodecs::usm::UsmBuildInput input;
    input.key = config.keys.has_cri_key ? config.keys.cri_key : 0;
    input.encrypt_audio = config.encrypt_audio;

    bool has_video = false;
    for (const auto& track : config.existing_usm_tracks) {
        if (!track.enabled) {
            continue;
        }

        std::filesystem::path source_path;
        switch (track.kind) {
        case MediaBuildConfig::ExistingUsmTrack::Kind::Video: {
            if (has_video) {
                push_log(log, QStringLiteral("Skipping extra video stream %1; builder currently accepts one video stream.").arg(track.stream_index));
                continue;
            }
            if (track.replacement_source.empty()) {
                auto materialized = materialize_existing_track(*reader, track, stage_dir, log);
                if (!materialized) {
                    return std::unexpected(materialized.error());
                }
                source_path = *materialized;
                config.video_prep = MediaVideoPrep::UsePrepared;
            } else {
                auto prepared = prepare_video_source(
                    track.replacement_source,
                    track.video_prep,
                    config.video_preset,
                    config.ffmpeg_path,
                    stage_dir,
                    log
                );
                if (!prepared) {
                    return std::unexpected(prepared.error());
                }
                source_path = *prepared;
            }
            input.video_path = source_path;
            has_video = true;
            break;
        }
        case MediaBuildConfig::ExistingUsmTrack::Kind::Audio: {
            if (track.replacement_source.empty()) {
                auto materialized = materialize_existing_track(*reader, track, stage_dir, log);
                if (!materialized) {
                    return std::unexpected(materialized.error());
                }
                source_path = *materialized;
            } else {
                auto prepared = prepare_audio_source(
                    track.replacement_source,
                    track.audio_prep,
                    config.ffmpeg_path,
                    stage_dir,
                    "replacement_audio_" + std::to_string(track.stream_index),
                    log
                );
                if (!prepared) {
                    return std::unexpected(prepared.error());
                }
                if (!prepared->has_value()) {
                    continue;
                }
                source_path = **prepared;
            }
            input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                .path = source_path,
                .channel_no = track.channel,
            });
            break;
        }
        case MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle: {
            if (track.replacement_source.empty()) {
                auto materialized = materialize_existing_track(*reader, track, stage_dir, log);
                if (!materialized) {
                    return std::unexpected(materialized.error());
                }
                source_path = *materialized;
            } else {
                source_path = track.replacement_source;
            }
            input.subtitle_tracks.push_back(cricodecs::usm::UsmBuildInput::SubtitleTrack{
                .path = source_path,
                .language_id = track.subtitle_language_id,
                .format = track.subtitle_format,
                .channel_no = track.channel,
            });
            break;
        }
        case MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported: {
            if (track.enabled) {
                return std::unexpected(QStringLiteral("USM rebuild does not support stream %1 (%2). Disable it before building.")
                    .arg(track.stream_index)
                    .arg(utf8_to_qstring(track.label)));
            }
            break;
        }
        }
    }

    if (!has_video) {
        return std::unexpected(QStringLiteral("Current USM build requires one enabled video stream."));
    }

    cricodecs::usm::UsmBuilder builder;
    push_log(log, QStringLiteral("USM current-file rebuild: %1").arg(path_to_qstring(output_path)));
    if (auto result = builder.build_to_file(output_path, input); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    return {};
}

} // namespace

std::optional<QString> validate_media_build_config(const MediaBuildConfig& config, bool inspect_sources) {
    if (config.output_path.empty()) {
        return QStringLiteral("Choose an output path.");
    }
    if (config.output_path.filename().empty()) {
        return QStringLiteral("Choose an output filename, not only a directory.");
    }

    const bool existing_build = is_existing_usm_build(config);
    if (existing_build) {
        if (config.target != MediaBuildTarget::Usm) {
            return QStringLiteral("Existing USM streams can only be rebuilt as USM.");
        }
        if (config.existing_usm_path.empty() && !config.has_existing_usm_bytes && config.existing_usm_bytes.empty()) {
            return QStringLiteral("Current USM source data is not available.");
        }

        int video_count = 0;
        bool needs_ffmpeg = false;
        for (const auto& track : config.existing_usm_tracks) {
            if (!track.enabled) {
                continue;
            }
            switch (track.kind) {
            case MediaBuildConfig::ExistingUsmTrack::Kind::Video:
                ++video_count;
                needs_ffmpeg = needs_ffmpeg ||
                    (!track.replacement_source.empty() && track.video_prep != MediaVideoPrep::UsePrepared);
                break;
            case MediaBuildConfig::ExistingUsmTrack::Kind::Audio:
                needs_ffmpeg = needs_ffmpeg ||
                    (!track.replacement_source.empty() && track.audio_prep != MediaAudioPrep::UsePrepared);
                break;
            case MediaBuildConfig::ExistingUsmTrack::Kind::Subtitle:
                break;
            case MediaBuildConfig::ExistingUsmTrack::Kind::Unsupported:
                return QStringLiteral("Disable unsupported stream %1 before rebuilding.").arg(track.stream_index);
            }
            if (inspect_sources && !track.replacement_source.empty() &&
                !std::filesystem::exists(track.replacement_source)) {
                return QStringLiteral("Replacement path does not exist: %1")
                    .arg(path_to_qstring(track.replacement_source));
            }
        }
        if (video_count != 1) {
            return QStringLiteral("Enable exactly one video stream.");
        }
        if (needs_ffmpeg && config.ffmpeg_path.empty()) {
            return QStringLiteral("FFmpeg was not found in PATH.");
        }
        if (config.encrypt_audio && !config.keys.has_cri_key) {
            return QStringLiteral("Audio encryption requires a configured CRI key.");
        }
        if (inspect_sources && !config.existing_usm_path.empty() &&
            !std::filesystem::exists(config.existing_usm_path)) {
            return QStringLiteral("Current USM path does not exist.");
        }
        return std::nullopt;
    }

    if (config.video_source.empty()) {
        return QStringLiteral("Choose a video source.");
    }
    if (config.target == MediaBuildTarget::Sfd &&
        (config.video_prep == MediaVideoPrep::FfmpegH264 || config.video_prep == MediaVideoPrep::FfmpegVp9)) {
        return QStringLiteral("SFD requires MPEG video. Choose prepared MPEG, MPEG-1, or MPEG-2.");
    }
    if (config.target == MediaBuildTarget::Sfd && config.audio_tracks.size() > 1) {
        return QStringLiteral("SFD supports at most one audio track.");
    }
    if (config.target == MediaBuildTarget::Sfd && !config.subtitle_tracks.empty()) {
        return QStringLiteral("SFD subtitle authoring is not supported.");
    }

    bool needs_ffmpeg = config.video_prep != MediaVideoPrep::UsePrepared;
    std::array<bool, 256> audio_channels{};
    for (const auto& track : config.audio_tracks) {
        if (track.source.empty()) {
            return QStringLiteral("Remove empty audio tracks or choose their source files.");
        }
        needs_ffmpeg = needs_ffmpeg || track.prep != MediaAudioPrep::UsePrepared;
        if (track.channel_no.has_value()) {
            if (audio_channels[*track.channel_no]) {
                return QStringLiteral("Audio channel %1 is assigned more than once.").arg(*track.channel_no);
            }
            audio_channels[*track.channel_no] = true;
        }
    }

    if (config.encrypt_audio && config.target != MediaBuildTarget::Usm) {
        return QStringLiteral("USM audio encryption is not available for SFD.");
    }
    if (config.encrypt_audio && !config.keys.has_cri_key) {
        return QStringLiteral("Audio encryption requires a configured CRI key.");
    }

    std::array<bool, 256> subtitle_channels{};
    for (const auto& track : config.subtitle_tracks) {
        if (track.source.empty()) {
            return QStringLiteral("Remove empty subtitle tracks or choose their source files.");
        }
        if (track.channel_no.has_value()) {
            if (subtitle_channels[*track.channel_no]) {
                return QStringLiteral("Subtitle channel %1 is assigned more than once.").arg(*track.channel_no);
            }
            subtitle_channels[*track.channel_no] = true;
        }
    }

    if (needs_ffmpeg && config.ffmpeg_path.empty()) {
        return QStringLiteral("FFmpeg was not found in PATH.");
    }
    if (!inspect_sources) {
        return std::nullopt;
    }
    if (!std::filesystem::exists(config.video_source)) {
        return QStringLiteral("Video source does not exist: %1").arg(path_to_qstring(config.video_source));
    }
    for (const auto& track : config.audio_tracks) {
        if (!std::filesystem::exists(track.source)) {
            return QStringLiteral("Audio source does not exist: %1").arg(path_to_qstring(track.source));
        }
    }
    for (const auto& track : config.subtitle_tracks) {
        if (!std::filesystem::exists(track.source)) {
            return QStringLiteral("Subtitle source does not exist: %1").arg(path_to_qstring(track.source));
        }
    }

    if (config.target == MediaBuildTarget::Sfd && config.video_prep == MediaVideoPrep::UsePrepared) {
        cricodecs::video::MpegVideoReader video;
        if (auto opened = video.open(config.video_source); !opened) {
            return QStringLiteral("Prepared SFD video is not an MPEG elementary stream: %1")
                .arg(utf8_to_qstring(opened.error()));
        }
    }
    if (config.target == MediaBuildTarget::Sfd && !config.audio_tracks.empty() &&
        config.audio_tracks.front().prep == MediaAudioPrep::UsePrepared) {
        auto audio = cricodecs::adx::Adx::load(config.audio_tracks.front().source);
        if (!audio || audio->is_ahx()) {
            return QStringLiteral("Prepared SFD audio must be ADX.");
        }
    }
    return std::nullopt;
}

std::expected<void, QString> build_media_from_sources(MediaBuildConfig config, MediaBuildLogCallback log) {
    if (const auto error = validate_media_build_config(config, true)) {
        return std::unexpected(*error);
    }
    if (config.output_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(config.output_path.parent_path(), ec);
        if (ec) {
            return std::unexpected(QStringLiteral("Could not create output directory: %1").arg(QString::fromStdString(ec.message())));
        }
    }

    QLockFile output_lock(path_to_qstring(config.output_path) + QStringLiteral(".cristudio.lock"));
    output_lock.setStaleLockTime(0);
    if (!output_lock.tryLock()) {
        return std::unexpected(QStringLiteral("Another build is already writing this output path."));
    }

    const auto stage_base = config.output_path.has_parent_path()
        ? config.output_path.parent_path()
        : std::filesystem::temp_directory_path();
    QTemporaryDir stage(path_to_qstring(stage_base / ".cristudio-build-XXXXXX"));
    if (!stage.isValid()) {
        return std::unexpected(QStringLiteral("Could not create build staging directory: %1").arg(stage.errorString()));
    }
    const auto stage_dir = path_from_qstring(stage.path());
    const auto staged_output = stage_dir /
        (config.target == MediaBuildTarget::Usm ? "result.usm" : "result.sfd");
    push_log(log, QStringLiteral("Build staging directory: %1").arg(path_to_qstring(stage_dir)));

    if (is_existing_usm_build(config)) {
        auto result = build_existing_usm_from_tracks(config, stage_dir, staged_output, log);
        if (!result) {
            return std::unexpected(result.error());
        }
        if (auto published = publish_staged_file(staged_output, config.output_path); !published) {
            return std::unexpected(published.error());
        }
        push_log(log, QStringLiteral("Built media output: %1").arg(path_to_qstring(config.output_path)));
        return {};
    }

    auto video_path = prepare_video_source(
        config.video_source,
        config.video_prep,
        config.video_preset,
        config.ffmpeg_path,
        stage_dir,
        log
    );
    if (!video_path) {
        return std::unexpected(video_path.error());
    }

    std::vector<std::filesystem::path> audio_paths;
    audio_paths.reserve(config.audio_tracks.size());
    for (size_t index = 0; index < config.audio_tracks.size(); ++index) {
        const auto& track = config.audio_tracks[index];
        auto prepared = prepare_audio_source(
            track.source,
            track.prep,
            config.ffmpeg_path,
            stage_dir,
            "audio_" + std::to_string(index),
            log
        );
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        if (prepared->has_value()) {
            audio_paths.push_back(**prepared);
        }
    }

    if (config.target == MediaBuildTarget::Usm) {
        cricodecs::usm::UsmBuildInput input;
        input.video_path = *video_path;
        input.key = config.keys.has_cri_key ? config.keys.cri_key : 0;
        input.encrypt_audio = config.encrypt_audio;
        for (size_t index = 0; index < audio_paths.size(); ++index) {
            const auto& source = config.audio_tracks[index];
            input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                .path = audio_paths[index],
                .channel_no = source.channel_no,
            });
        }
        for (const auto& subtitle : config.subtitle_tracks) {
            input.subtitle_tracks.push_back(cricodecs::usm::UsmBuildInput::SubtitleTrack{
                .path = subtitle.source,
                .language_id = subtitle.language_id,
                .format = subtitle.format,
                .channel_no = subtitle.channel_no,
            });
            push_log(log, QStringLiteral("USM subtitle source: %1, language %2.")
                .arg(path_to_qstring(subtitle.source))
                .arg(subtitle.language_id));
        }
        cricodecs::usm::UsmBuilder builder;
        push_log(log, QStringLiteral("USM native build: %1").arg(path_to_qstring(staged_output)));
        if (auto result = builder.build_to_file(staged_output, input); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
    } else {
        cricodecs::sfd::SfdBuildInput input;
        input.video_path = *video_path;
        if (!audio_paths.empty()) {
            input.audio_path = audio_paths.front();
        }
        input.output_name = config.output_path.filename().string();
        input.build_profile = config.sfd_profile;
        cricodecs::sfd::SfdBuilder builder;
        push_log(log, QStringLiteral("SFD native build: %1").arg(path_to_qstring(staged_output)));
        if (auto result = builder.build_to_file(staged_output, input); !result) {
            return std::unexpected(utf8_to_qstring(result.error()));
        }
    }

    if (auto published = publish_staged_file(staged_output, config.output_path); !published) {
        return std::unexpected(published.error());
    }

    push_log(log, QStringLiteral("Built media output: %1").arg(path_to_qstring(config.output_path)));
    return {};
}

std::vector<TransformDetailRow> media_build_job_detail_rows(const DecryptionKeys& keys) {
    return {
        {QStringLiteral("Job"), QStringLiteral("Build USM or SFD from video and optional audio sources")},
        {QStringLiteral("Video prep"), QStringLiteral("prepared source, FFmpeg VP9, H.264, MPEG-1, or MPEG-2")},
        {QStringLiteral("Audio prep"), QStringLiteral("no tracks, or all ADX/HCA from prepared or FFmpeg-supported audio")},
        {QStringLiteral("Outputs"), QStringLiteral(".usm, .sfd")},
        {
            QStringLiteral("Keys"),
            keys.has_cri_key
                ? QStringLiteral("CRI key configured for USM ADX masking / HCA cipher-56")
                : QStringLiteral("no CRI key configured")
        }
    };
}

} // namespace cristudio::modules::usm
