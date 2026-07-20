#include "shared/ffmpeg_audio_preview.hpp"

#include "main_window/preview_helpers.hpp"
#include "wav_container.hpp"

#include <QFile>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include <limits>
#include <utility>
#include <vector>

namespace cristudio {

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

std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    cricodecs::awb::EntryCodec codec,
    std::span<const uint8_t> bytes
) {
    if (!is_ffmpeg_audio_codec(codec)) {
        return std::unexpected("audio preview does not use ffmpeg for " +
            std::string(cricodecs::awb::entry_codec_name(codec)));
    }

    const auto ffmpeg = find_ffmpeg_executable();
    if (ffmpeg.isEmpty()) {
        return std::unexpected("audio preview requires ffmpeg for " +
            std::string(cricodecs::awb::entry_codec_name(codec)));
    }
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<qint64>::max)())) {
        return std::unexpected("audio preview input is too large for ffmpeg");
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        return std::unexpected("audio preview could not create a temporary ffmpeg directory");
    }
    const auto extension = cricodecs::awb::entry_codec_extension(codec);
    const auto input_path = directory.filePath(
        QStringLiteral("input") + QString::fromLatin1(extension.data(), static_cast<qsizetype>(extension.size())));
    const auto output_path = directory.filePath(QStringLiteral("preview.wav"));
    QFile input(input_path);
    if (!input.open(QIODevice::WriteOnly) ||
        input.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) !=
            static_cast<qint64>(bytes.size())) {
        return std::unexpected("audio preview could not stage the ffmpeg input");
    }
    input.close();

    QProcess process;
    process.start(ffmpeg, {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), input_path,
        QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-vn"),
        output_path,
    });
    if (!process.waitForStarted(5000)) {
        return std::unexpected("audio preview could not start ffmpeg: " + process.errorString().toStdString());
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return std::unexpected("audio preview ffmpeg decode timed out");
    }
    const auto stderr_text = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::unexpected("audio preview ffmpeg decode failed" +
            (stderr_text.isEmpty() ? std::string{} : ": " + stderr_text.toStdString()));
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
    preview.format = std::string(cricodecs::awb::entry_codec_name(codec));
    preview.note = "Decoded with ffmpeg";
    return preview;
}

} // namespace cristudio
