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

bool is_obviously_non_media(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return true;
    }

    static constexpr std::array<std::string_view, 10> non_media_signatures{
        std::string_view("PK\x03\x04", 4),
        std::string_view("\x7F" "ELF", 4),
        std::string_view("MZ", 2),
        std::string_view("%PDF", 4),
        std::string_view("SQLite format 3\0", 16),
        std::string_view("Rar!\x1A\x07", 7),
        std::string_view("7z\xBC\xAF\x27\x1C", 6),
        std::string_view("\x1F\x8B", 2),
        std::string_view("BZh", 3),
        std::string_view("\xFD" "7zXZ\0", 6),
    };
    if (std::ranges::any_of(non_media_signatures, [bytes](std::string_view signature) {
            return has_prefix(bytes, signature);
        })) {
        return true;
    }

    size_t text_bytes = 0;
    for (const auto byte : bytes) {
        if (byte == 0) {
            return false;
        }
        if (byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 0x20 && byte < 0x7F)) {
            ++text_bytes;
        }
    }
    return text_bytes * 100u >= bytes.size() * 95u;
}

std::expected<void, std::string> write_preview_input(
    QFile& output,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return std::unexpected("preview canceled");
        }
        const auto size = std::min(write_chunk_size, bytes.size() - offset);
        if (output.write(
                reinterpret_cast<const char*>(bytes.data() + offset),
                static_cast<qint64>(size)) != static_cast<qint64>(size)) {
            return std::unexpected("media preview could not stage the ffmpeg input");
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
        return std::unexpected("preview canceled");
    }

    const auto ffmpeg = find_ffmpeg_executable();
    if (ffmpeg.isEmpty()) {
        return std::unexpected(std::string(operation) + " requires ffmpeg");
    }

    QProcess process;
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(ffmpeg_start_timeout_ms)) {
        return std::unexpected(
            std::string(operation) + " could not start ffmpeg: " +
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
            return std::unexpected("preview canceled");
        }
        const auto remaining = timeout_ms - static_cast<int>(elapsed.elapsed());
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected(std::string(operation) + " timed out");
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
            std::string(operation) + " failed" +
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
    }, ffmpeg_probe_timeout_ms, stop_token, "media preview probe");
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
        return std::unexpected("media preview probe found no playable audio or video stream");
    }

    if (probe.has_video && probe.has_audio) {
        probe.format = "FFmpeg video with audio";
    } else if (probe.has_video) {
        probe.format = "FFmpeg video";
    } else {
        probe.format = "FFmpeg audio";
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
    }, ffmpeg_decode_timeout_ms, stop_token, "audio preview ffmpeg decode");
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    QFile output(output_path);
    if (!output.open(QIODevice::ReadOnly)) {
        return std::unexpected("audio preview could not read the ffmpeg WAV output");
    }
    const auto wav = output.readAll();
    if (wav.isEmpty()) {
        return std::unexpected("audio preview ffmpeg decode produced an empty WAV output");
    }

    std::vector<uint8_t> wav_bytes(
        reinterpret_cast<const uint8_t*>(wav.constData()),
        reinterpret_cast<const uint8_t*>(wav.constData()) + wav.size());
    cricodecs::wav::WavContainer container;
    if (auto loaded = container.load(wav_bytes); !loaded) {
        return std::unexpected("audio preview could not parse ffmpeg WAV output: " + loaded.error());
    }

    AudioPreview preview;
    preview.wav_bytes = std::move(wav_bytes);
    preview.sample_rate = container.sample_rate();
    preview.channels = static_cast<uint16_t>(container.channels());
    preview.sample_count = container.sample_count();
    preview.format = std::move(format);
    preview.note = "Decoded with ffmpeg";
    return preview;
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_staged_bytes(
    std::span<const uint8_t> bytes,
    QString input_suffix,
    std::string format,
    std::stop_token stop_token
) {
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<qint64>::max)())) {
        return std::unexpected("audio preview input is too large for ffmpeg");
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected("audio preview could not create a temporary ffmpeg directory");
    }
    const auto input_path = directory.filePath(QStringLiteral("input") + input_suffix);
    const auto output_path = directory.filePath(QStringLiteral("preview.wav"));
    QFile input(input_path);
    if (!input.open(QIODevice::WriteOnly)) {
        return std::unexpected("audio preview could not stage the ffmpeg input");
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
        return true;
    case EntryCodec::Unknown:
    case EntryCodec::Hca:
    case EntryCodec::Adx:
    case EntryCodec::Ahx:
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
        return std::unexpected("media preview probe could not read the input");
    }
    const auto prefix = input.read(static_cast<qint64>(media_prefilter_size));
    const auto prefix_bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(prefix.constData()),
        static_cast<size_t>(prefix.size())
    );
    if (is_obviously_non_media(prefix_bytes)) {
        return std::unexpected("media preview probe rejected obvious non-media content");
    }
    return probe_ffmpeg_media_path(path_to_qstring(path), stop_token);
}

std::expected<FfmpegMediaProbe, std::string> probe_ffmpeg_media_bytes(
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<qint64>::max)())) {
        return std::unexpected("media preview input is too large for ffmpeg");
    }
    if (is_obviously_non_media(bytes.first(std::min(bytes.size(), media_prefilter_size)))) {
        return std::unexpected("media preview probe rejected obvious non-media content");
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected("media preview could not create a temporary ffmpeg directory");
    }
    const auto input_path = directory.filePath(QStringLiteral("input.bin"));
    QFile input(input_path);
    if (!input.open(QIODevice::WriteOnly)) {
        return std::unexpected("media preview could not stage the ffmpeg input");
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
        return std::unexpected("extraction canceled");
    }
    if (!is_ffmpeg_audio_codec(codec)) {
        return std::unexpected("audio preview does not use ffmpeg for " +
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
        "FFmpeg audio",
        stop_token
    );
}

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_file(
    const std::filesystem::path& path,
    std::stop_token stop_token
) {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected("audio preview could not create a temporary ffmpeg directory");
    }
    return decode_ffmpeg_audio(
        path_to_qstring(path),
        directory.filePath(QStringLiteral("preview.wav")),
        "FFmpeg audio",
        stop_token
    );
}

} // namespace cristudio
