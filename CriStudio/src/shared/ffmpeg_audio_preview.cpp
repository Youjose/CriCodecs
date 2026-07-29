#include "shared/i18n.hpp"
#include "shared/ffmpeg_audio_preview.hpp"

#include "main_window/preview_helpers.hpp"
#include "path_text.hpp"
#include "wav_container.hpp"

#include <QByteArray>
#include <QFile>
#include <QElapsedTimer>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <limits>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

constexpr int ffmpeg_start_timeout_ms = 5000;
constexpr int ffmpeg_probe_timeout_ms = 10000;
constexpr int ffmpeg_decode_timeout_ms = 30000;
constexpr size_t write_chunk_size = 4u * 1024u * 1024u;
constexpr size_t media_prefilter_size = 4096;

bool has_prefix(std::span<const uint8_t> bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() &&
        std::equal(
            prefix.begin(),
            prefix.end(),
            bytes.begin(),
            [](char expected, uint8_t actual) {
                return static_cast<uint8_t>(expected) == actual;
            }
        );
}

bool has_bytes_at(std::span<const uint8_t> bytes, size_t offset, std::string_view value) {
    return offset <= bytes.size() && value.size() <= bytes.size() - offset &&
        has_prefix(bytes.subspan(offset), value);
}

bool has_transport_stream_sync(std::span<const uint8_t> bytes, size_t packet_size) {
    return bytes.size() > packet_size * 2u &&
        bytes[0] == 0x47 &&
        bytes[packet_size] == 0x47 &&
        bytes[packet_size * 2u] == 0x47;
}

bool has_annex_b_video_prefix(std::span<const uint8_t> bytes) {
    const size_t nal_offset =
        has_bytes_at(bytes, 0, std::string_view("\0\0\0\x01", 4)) ? 4u :
        has_bytes_at(bytes, 0, std::string_view("\0\0\x01", 3)) ? 3u : 0u;
    if (nal_offset == 0 || nal_offset >= bytes.size()) {
        return false;
    }
    const auto h264_type = bytes[nal_offset] & 0x1Fu;
    const auto h265_type = (bytes[nal_offset] >> 1u) & 0x3Fu;
    return h264_type == 7 || h264_type == 9 ||
        h265_type == 32 || h265_type == 33 || h265_type == 35;
}

bool is_likely_common_media(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return false;
    }

    const auto codec = cricodecs::awb::probe_entry_codec(bytes);
    if (codec == cricodecs::awb::EntryCodec::Hca ||
        codec == cricodecs::awb::EntryCodec::Adx ||
        codec == cricodecs::awb::EntryCodec::Ahx) {
        return false;
    }
    if (codec != cricodecs::awb::EntryCodec::Unknown) {
        return true;
    }

    static constexpr std::array<std::string_view, 13> prefixes{
        std::string_view("\x1A\x45\xDF\xA3", 4),
        "FLV",
        "DKIF",
        ".RMF",
        "caff",
        ".snd",
        "MThd",
        "wvpk",
        "MAC ",
        "MPCK",
        "#!AMR",
        std::string_view("\x0B\x77", 2),
        std::string_view("\x30\x26\xB2\x75\x8E\x66\xCF\x11"
                         "\xA6\xD9\x00\xAA\x00\x62\xCE\x6C", 16),
    };
    if (std::ranges::any_of(prefixes, [bytes](std::string_view signature) {
            return has_prefix(bytes, signature);
        })) {
        return true;
    }

    if (has_bytes_at(bytes, 4, "ftyp") ||
        has_bytes_at(bytes, 4, "moov") ||
        has_bytes_at(bytes, 4, "mdat") ||
        has_bytes_at(bytes, 4, "wide")) {
        return true;
    }
    if (has_prefix(bytes, "RIFF") &&
        (has_bytes_at(bytes, 8, "AVI ") || has_bytes_at(bytes, 8, "WAVE"))) {
        return true;
    }
    if (has_prefix(bytes, "FORM") &&
        (has_bytes_at(bytes, 8, "AIFF") || has_bytes_at(bytes, 8, "AIFC"))) {
        return true;
    }
    if (has_bytes_at(bytes, 0, std::string_view("\0\0\x01\xBA", 4)) ||
        has_bytes_at(bytes, 0, std::string_view("\0\0\x01\xB3", 4)) ||
        has_annex_b_video_prefix(bytes)) {
        return true;
    }
    if (has_transport_stream_sync(bytes, 188) ||
        has_transport_stream_sync(bytes, 192) ||
        has_transport_stream_sync(bytes, 204)) {
        return true;
    }

    static constexpr std::array<std::string_view, 4> dts_prefixes{
        std::string_view("\x7F\xFE\x80\x01", 4),
        std::string_view("\xFE\x7F\x01\x80", 4),
        std::string_view("\x1F\xFF\xE8\x00", 4),
        std::string_view("\xFF\x1F\x00\xE8", 4),
    };
    return std::ranges::any_of(dts_prefixes, [bytes](std::string_view signature) {
        return has_prefix(bytes, signature);
    });
}

std::expected<void, std::string> write_preview_input(
    QFile& output,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "preview canceled"));
        }
        const auto size = std::min(write_chunk_size, bytes.size() - offset);
        if (output.write(
                reinterpret_cast<const char*>(bytes.data() + offset),
                static_cast<qint64>(size)) != static_cast<qint64>(size)) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview could not stage the ffmpeg input"));
        }
        offset += size;
    }
    return {};
}

std::expected<QByteArray, std::string> run_ffmpeg(
    const QStringList& arguments,
    int timeout_ms,
    std::stop_token stop_token,
    std::string_view operation
) {
    if (stop_token.stop_requested()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "preview canceled"));
    }

    const auto ffmpeg = find_ffmpeg_executable();
    if (ffmpeg.isEmpty()) {
        return std::unexpected(std::string(operation) + cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", " requires ffmpeg"));
    }

    QProcess process;
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(ffmpeg_start_timeout_ms)) {
        return std::unexpected(
            std::string(operation) + cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", " could not start ffmpeg: ") +
            process.errorString().toStdString()
        );
    }

    QByteArray stderr_bytes;
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning) {
        if (stop_token.stop_requested()) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "preview canceled"));
        }
        const auto remaining = timeout_ms - static_cast<int>(elapsed.elapsed());
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected(std::string(operation) + cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", " timed out"));
        }
        process.waitForFinished(std::min(remaining, 100));
        stderr_bytes += process.readAllStandardError();
        static_cast<void>(process.readAllStandardOutput());
    }

    stderr_bytes += process.readAllStandardError();
    static_cast<void>(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const auto detail = QString::fromLocal8Bit(stderr_bytes).trimmed();
        return std::unexpected(
            std::string(operation) + cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", " failed") +
            (detail.isEmpty() ? std::string{} : ": " + detail.toStdString())
        );
    }
    return stderr_bytes;
}

std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_path(
    const QString& input_path,
    std::stop_token stop_token
) {
    auto result = run_ffmpeg({
        QStringLiteral("-hide_banner"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("info"),
        QStringLiteral("-i"), input_path,
        QStringLiteral("-map"), QStringLiteral("0:v:0?"),
        QStringLiteral("-map"), QStringLiteral("0:a:0?"),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-frames:a"), QStringLiteral("1"),
        QStringLiteral("-f"), QStringLiteral("null"),
        QStringLiteral("-"),
    }, ffmpeg_probe_timeout_ms, stop_token, cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview probe"));
    if (!result) {
        return std::unexpected(result.error());
    }

    FfmpegMediaProbe probe;
    const auto lines = QString::fromLocal8Bit(*result).split(QLatin1Char('\n'));
    for (const auto& line : lines) {
        if (!line.contains(QStringLiteral("Stream #"))) {
            continue;
        }
        probe.has_audio = probe.has_audio || line.contains(QStringLiteral("Audio:"));
        probe.has_video = probe.has_video || line.contains(QStringLiteral("Video:"));
    }
    if (!probe.has_audio && !probe.has_video) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview probe found no playable audio or video stream"));
    }

    if (probe.has_video && probe.has_audio) {
        probe.format = cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "FFmpeg video with audio");
    } else if (probe.has_video) {
        probe.format = cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "FFmpeg video");
    } else {
        probe.format = cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "FFmpeg audio");
    }
    return probe;
}

std::expected<AudioPreview, std::string> decode_ffmpeg_audio(
    const QString& input_path,
    const QString& output_path,
    std::string format,
    std::stop_token stop_token
) {
    auto decoded = run_ffmpeg({
        QStringLiteral("-hide_banner"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), input_path,
        QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-vn"),
        output_path,
    }, ffmpeg_decode_timeout_ms, stop_token, cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview ffmpeg decode"));
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    QFile output(output_path);
    if (!output.open(QIODevice::ReadOnly)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview could not read the ffmpeg WAV output"));
    }
    const auto wav = output.readAll();
    if (wav.isEmpty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview ffmpeg decode produced an empty WAV output"));
    }

    std::vector<uint8_t> wav_bytes(
        reinterpret_cast<const uint8_t*>(wav.constData()),
        reinterpret_cast<const uint8_t*>(wav.constData()) + wav.size());
    cricodecs::wav::WavContainer container;
    if (auto loaded = container.load(wav_bytes); !loaded) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview could not parse ffmpeg WAV output: ") + loaded.error());
    }

    AudioPreview preview;
    preview.wav_bytes = std::move(wav_bytes);
    preview.sample_rate = container.sample_rate();
    preview.channels = static_cast<uint16_t>(container.channels());
    preview.sample_count = container.sample_count();
    preview.format = std::move(format);
    preview.note = cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "Decoded with ffmpeg");
    return preview;
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_staged_bytes(
    std::span<const uint8_t> bytes,
    QString input_suffix,
    std::string format,
    std::stop_token stop_token
) {
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<qint64>::max)())) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview input is too large for ffmpeg"));
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview could not create a temporary ffmpeg directory"));
    }
    const auto input_path = directory.filePath(QStringLiteral("input") + input_suffix);
    const auto output_path = directory.filePath(QStringLiteral("preview.wav"));
    QFile input(input_path);
    if (!input.open(QIODevice::WriteOnly)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview could not stage the ffmpeg input"));
    }
    auto written = write_preview_input(input, bytes, stop_token);
    input.close();
    if (!written) {
        return std::unexpected(written.error());
    }
    return decode_ffmpeg_audio(input_path, output_path, std::move(format), stop_token);
}

} // namespace

bool is_ffmpeg_audio_codec(cricodecs::awb::EntryCodec codec) {
    using cricodecs::awb::EntryCodec;
    switch (codec) {
    case EntryCodec::AacM4a:
    case EntryCodec::AacAdts:
    case EntryCodec::OggVorbis:
    case EntryCodec::OggOpus:
    case EntryCodec::OggSpeex:
    case EntryCodec::Ogg:
    case EntryCodec::Flac:
    case EntryCodec::Mp3:
    case EntryCodec::Vag:
    case EntryCodec::Atrac3:
    case EntryCodec::Atrac9:
        return true;
    case EntryCodec::Unknown:
    case EntryCodec::Hca:
    case EntryCodec::HcaMx:
    case EntryCodec::Adx:
    case EntryCodec::Ahx:
    case EntryCodec::SwLpcm:
    case EntryCodec::DsAdpcm:
    case EntryCodec::NintendoDsp:
    case EntryCodec::WiiAdpcm:
    case EntryCodec::WiiUAdpcm:
    case EntryCodec::Hevag:
    case EntryCodec::ThreeDsAdpcm:
    case EntryCodec::Xma2:
    case EntryCodec::SwitchOpus:
    case EntryCodec::Wave:
        return false;
    }
    return false;
}

std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_file(
    const std::filesystem::path& path,
    std::stop_token stop_token
) {
    QFile input(path_to_qstring(path));
    if (!input.open(QIODevice::ReadOnly)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview probe could not read the input"));
    }
    const auto prefix = input.read(static_cast<qint64>(media_prefilter_size));
    const auto prefix_bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(prefix.constData()),
        static_cast<size_t>(prefix.size())
    );
    if (!is_likely_common_media(prefix_bytes)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview probe rejected unsupported content"));
    }
    return probe_ffmpeg_media_path(path_to_qstring(path), stop_token);
}

std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_bytes(
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<qint64>::max)())) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview input is too large for ffmpeg"));
    }
    if (!is_likely_common_media(bytes.first(std::min(bytes.size(), media_prefilter_size)))) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview probe rejected unsupported content"));
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview could not create a temporary ffmpeg directory"));
    }
    const auto input_path = directory.filePath(QStringLiteral("input.bin"));
    QFile input(input_path);
    if (!input.open(QIODevice::WriteOnly)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "media preview could not stage the ffmpeg input"));
    }
    auto written = write_preview_input(input, bytes, stop_token);
    input.close();
    if (!written) {
        return std::unexpected(written.error());
    }
    return probe_ffmpeg_media_path(input_path, stop_token);
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    cricodecs::awb::EntryCodec codec,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "extraction canceled"));
    }
    if (!is_ffmpeg_audio_codec(codec)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview does not use ffmpeg for ") +
            std::string(cricodecs::awb::entry_codec_name(codec)));
    }

    const auto extension = cricodecs::awb::entry_codec_extension(codec);
    return ffmpeg_audio_preview_from_staged_bytes(
        bytes,
        QString::fromLatin1(extension.data(), static_cast<qsizetype>(extension.size())),
        std::string(cricodecs::awb::entry_codec_name(codec)),
        stop_token
    );
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    return ffmpeg_audio_preview_from_staged_bytes(
        bytes,
        QStringLiteral(".bin"),
        cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "FFmpeg audio"),
        stop_token
    );
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_file(
    const std::filesystem::path& path,
    std::stop_token stop_token
) {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "audio preview could not create a temporary ffmpeg directory"));
    }
    return decode_ffmpeg_audio(
        path_to_qstring(path),
        directory.filePath(QStringLiteral("preview.wav")),
        cristudio::i18n::translate_utf8("Shared.FfmpegAudioPreview", "FFmpeg audio"),
        stop_token
    );
}

} // namespace cristudio
