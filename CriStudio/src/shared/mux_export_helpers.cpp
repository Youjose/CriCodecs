#include "shared/mux_export_helpers.hpp"

#include "path_text.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <stop_token>
#include <string_view>

namespace cristudio {
namespace {

std::string ffmpeg_error_text(QProcess& process) {
    auto text = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (text.isEmpty()) {
        text = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }
    return text.toStdString();
}

bool has_video_decode_error(std::string_view stderr_text) {
    auto lower = std::string(stderr_text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower.find("failed to read frame header") != std::string::npos ||
           lower.find("failed to read unit") != std::string::npos ||
           lower.find("invalid value") != std::string::npos ||
           lower.find("invalid le value") != std::string::npos ||
           lower.find("invalid data found when processing input") != std::string::npos ||
           lower.find("extra padding at end of superframe") != std::string::npos ||
           lower.find("unexpected ffmpeg behavior") != std::string::npos ||
           lower.find("not all references are available") != std::string::npos ||
           lower.find("error submitting packet to decoder") != std::string::npos ||
           lower.find("error processing packet in decoder") != std::string::npos ||
           lower.find("error while decoding") != std::string::npos ||
           lower.find("could not find codec parameters") != std::string::npos;
}

std::expected<void, std::string> write_staged_bytes(
    QFile& file,
    const char* bytes,
    size_t size,
    std::string_view description,
    std::stop_token stop_token
) {
    constexpr size_t write_chunk_size = 4u * 1024u * 1024u;
    size_t written = 0;
    while (written < size) {
        if (stop_token.stop_requested()) {
            return std::unexpected("extraction canceled");
        }
        const auto chunk_size = static_cast<qint64>(std::min<size_t>(
            size - written,
            write_chunk_size
        ));
        const auto count = file.write(bytes + written, chunk_size);
        if (count <= 0) {
            return std::unexpected(
                "could not write temporary " + std::string(description) + ": " + file.errorString().toStdString());
        }
        written += static_cast<size_t>(count);
    }
    if (!file.flush()) {
        return std::unexpected(
            "could not flush temporary " + std::string(description) + ": " + file.errorString().toStdString());
    }
    return {};
}

std::expected<void, std::string> wait_for_process(
    QProcess& process,
    int timeout_ms,
    std::stop_token stop_token
) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning) {
        if (stop_token.stop_requested()) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected("extraction canceled");
        }
        const auto remaining = timeout_ms - static_cast<int>(elapsed.elapsed());
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected("ffmpeg timed out");
        }
        process.waitForFinished(std::min(remaining, 100));
    }
    return {};
}

std::expected<void, std::string> validate_mux_output_file(
    const std::filesystem::path& ffmpeg_path,
    const std::filesystem::path& output_path,
    std::stop_token stop_token
) {
    QProcess ffmpeg_process;
    ffmpeg_process.start(
        path_to_qstring(ffmpeg_path),
        QStringList{
            QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"),
            QStringLiteral("error"),
            QStringLiteral("-xerror"),
            QStringLiteral("-y"),
            QStringLiteral("-i"),
            path_to_qstring(output_path),
            QStringLiteral("-map"),
            QStringLiteral("0:v:0"),
            QStringLiteral("-an"),
            QStringLiteral("-frames:v"),
            QStringLiteral("32"),
            QStringLiteral("-f"),
            QStringLiteral("null"),
            QStringLiteral("-"),
        }
    );
    const auto finished = wait_for_process(ffmpeg_process, 10000, stop_token);
    if (!finished) {
        return std::unexpected(finished.error());
    }
    const auto stderr_text = ffmpeg_error_text(ffmpeg_process);
    if (
        ffmpeg_process.exitStatus() == QProcess::NormalExit &&
        ffmpeg_process.exitCode() == 0 &&
        !has_video_decode_error(stderr_text)
    ) {
        return {};
    }
    return std::unexpected("video may be encrypted or unsupported");
}

} // namespace

std::expected<void, std::string> write_mux_extract_file(
    const MuxPreview& mux,
    const std::filesystem::path& output_path,
    const std::filesystem::path& ffmpeg_path,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected("extraction canceled");
    }
    if (ffmpeg_path.empty()) {
        return std::unexpected("ffmpeg not configured for mux extraction");
    }
    if (mux.video_bytes.empty()) {
        return std::unexpected("mux extraction did not produce video bytes");
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-mux-extract-XXXXXX"));
    if (!temp_dir.isValid()) {
        return std::unexpected("could not create temporary mux extraction directory");
    }

    const auto video_suffix = mux.video_suffix.empty()
        ? QStringLiteral(".m2v")
        : utf8_to_qstring(mux.video_suffix);
    const auto video_path = temp_dir.filePath(QStringLiteral("video") + video_suffix);
    QFile video_file(video_path);
    if (!video_file.open(QIODevice::WriteOnly)) {
        return std::unexpected("could not write temporary mux video stream");
    }
    if (auto written = write_staged_bytes(
            video_file,
            reinterpret_cast<const char*>(mux.video_bytes.data()),
            mux.video_bytes.size(),
            "mux video stream",
            stop_token); !written) {
        return std::unexpected(written.error());
    }
    video_file.close();

    QString audio_path;
    if (!mux.audio_wav_bytes.empty()) {
        audio_path = temp_dir.filePath(QStringLiteral("audio.wav"));
        QFile audio_file(audio_path);
        if (!audio_file.open(QIODevice::WriteOnly)) {
            return std::unexpected("could not write temporary mux audio stream");
        }
        if (auto written = write_staged_bytes(
                audio_file,
                reinterpret_cast<const char*>(mux.audio_wav_bytes.data()),
                mux.audio_wav_bytes.size(),
                "mux audio stream",
                stop_token); !written) {
            return std::unexpected(written.error());
        }
        audio_file.close();
    }

    QStringList subtitle_paths;
    subtitle_paths.reserve(static_cast<qsizetype>(mux.subtitle_choices.size()));
    for (size_t index = 0; index < mux.subtitle_choices.size(); ++index) {
        if (stop_token.stop_requested()) {
            return std::unexpected("extraction canceled");
        }
        const auto& subtitle = mux.subtitle_choices[index];
        const auto subtitle_path = temp_dir.filePath(QStringLiteral("subtitle-%1.srt").arg(index));
        QFile subtitle_file(subtitle_path);
        if (!subtitle_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return std::unexpected("could not write temporary mux subtitle stream");
        }
        if (auto written = write_staged_bytes(
                subtitle_file,
                subtitle.srt_text.data(),
                subtitle.srt_text.size(),
                "mux subtitle stream",
                stop_token); !written) {
            return std::unexpected(written.error());
        }
        subtitle_file.close();
        subtitle_paths.push_back(subtitle_path);
    }

    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-y"),
    };
    if (mux.frame_rate_n != 0 && mux.frame_rate_d != 0) {
        arguments << QStringLiteral("-r") << QStringLiteral("%1/%2").arg(mux.frame_rate_n).arg(mux.frame_rate_d);
    }
    if (!mux.ffmpeg_input_format.empty()) {
        arguments << QStringLiteral("-f") << utf8_to_qstring(mux.ffmpeg_input_format);
    }
    arguments << QStringLiteral("-i") << video_path;
    if (!audio_path.isEmpty()) {
        arguments << QStringLiteral("-i") << audio_path;
    }
    for (const auto& subtitle_path : subtitle_paths) {
        arguments << QStringLiteral("-i") << subtitle_path;
    }
    arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (!audio_path.isEmpty()) {
        arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    }
    const auto subtitle_input_base = audio_path.isEmpty() ? 1 : 2;
    for (int i = 0; i < subtitle_paths.size(); ++i) {
        arguments << QStringLiteral("-map") << QStringLiteral("%1:0").arg(subtitle_input_base + i);
    }
    arguments << QStringLiteral("-c:v") << QStringLiteral("copy");
    if (!audio_path.isEmpty()) {
        arguments << QStringLiteral("-c:a") << QStringLiteral("copy");
    }
    if (!subtitle_paths.empty()) {
        arguments << QStringLiteral("-c:s") << QStringLiteral("srt");
        for (int i = 0; i < subtitle_paths.size(); ++i) {
            const auto& subtitle = mux.subtitle_choices[static_cast<size_t>(i)];
            arguments
                << QStringLiteral("-metadata:s:s:%1").arg(i)
                << QStringLiteral("title=language %1").arg(subtitle.language_id);
        }
    }
    arguments
        << QStringLiteral("-max_interleave_delta") << QStringLiteral("0")
        << QStringLiteral("-muxdelay") << QStringLiteral("0")
        << QStringLiteral("-muxpreload") << QStringLiteral("0")
        << QStringLiteral("-f") << QStringLiteral("matroska")
        << path_to_qstring(output_path);

    QProcess ffmpeg_process;
    ffmpeg_process.start(path_to_qstring(ffmpeg_path), arguments);
    const auto waited = wait_for_process(ffmpeg_process, 30000, stop_token);
    const auto mux_ok = waited &&
        ffmpeg_process.exitStatus() == QProcess::NormalExit &&
        ffmpeg_process.exitCode() == 0 &&
        QFileInfo::exists(path_to_qstring(output_path));
    if (!mux_ok) {
        std::error_code ec;
        std::filesystem::remove(output_path, ec);
        if (!waited) {
            return std::unexpected(waited.error());
        }
        return std::unexpected("ffmpeg stream-copy mux failed: " + ffmpeg_error_text(ffmpeg_process));
    }

    if (auto valid = validate_mux_output_file(ffmpeg_path, output_path, stop_token); !valid) {
        std::error_code ec;
        std::filesystem::remove(output_path, ec);
        return std::unexpected(valid.error());
    }
    return {};
}

} // namespace cristudio
