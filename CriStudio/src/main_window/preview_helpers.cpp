#include "preview_helpers.hpp"

#include "../path_text.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <filesystem>
#include <system_error>

namespace cristudio {
namespace {

template <typename Preview>
void remove_preview_directory(Preview& preview) {
    if (preview.temporary_directory.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove_all(preview.temporary_directory, error);
    preview.temporary_directory.clear();
    preview.playable_path.clear();
}

} // namespace

bool is_audio_document(const LoadedDocument& document) {
    const auto format = utf8_to_qstring(document.format).toLower();
    return format.contains(QStringLiteral("audio")) ||
           format.contains(QStringLiteral("adx")) ||
           format.contains(QStringLiteral("ahx")) ||
           format.contains(QStringLiteral("aax")) ||
           format.contains(QStringLiteral("hca")) ||
           format.contains(QStringLiteral("wav"));
}

bool is_direct_audio_document(const LoadedDocument& document) {
    const auto format = utf8_to_qstring(document.format).toLower();
    return format.contains(QStringLiteral("adx")) ||
           format.contains(QStringLiteral("ahx")) ||
           format.contains(QStringLiteral("aax")) ||
           format.contains(QStringLiteral("hca")) ||
           format.contains(QStringLiteral("wav"));
}

bool is_mux_document(const LoadedDocument& document) {
    const auto format = utf8_to_qstring(document.format).toLower();
    return format.contains(QStringLiteral("usm")) ||
           format.contains(QStringLiteral("sfd")) ||
           format.contains(QStringLiteral("sofdec"));
}

bool is_low_signal_loader_message(const QString& message) {
    const auto lower = message.toLower();
    return lower.contains(QStringLiteral("invalid magic")) ||
           lower.contains(QStringLiteral("invalid riff/wave header")) ||
           lower.contains(QStringLiteral("invalid hca signature")) ||
           lower.contains(QStringLiteral("missing adx signature")) ||
           lower.contains(QStringLiteral("expected utf table name")) ||
           lower.contains(QStringLiteral("does not start with"));
}


QString video_preview_unavailable_message() {
    return QStringLiteral(
        "Preview unavailable: video validation failed; this does not distinguish a masked stream from an unsupported or corrupt one"
    );
}

QString find_ffmpeg_executable() {
#ifdef Q_OS_WIN
    constexpr auto executable_name = "ffmpeg.exe";
#else
    constexpr auto executable_name = "ffmpeg";
#endif
    const QDir application_dir(QCoreApplication::applicationDirPath());
    for (const auto& relative_path : {
             QString::fromLatin1(executable_name),
             QStringLiteral("_internal/") + QString::fromLatin1(executable_name),
         }) {
        const auto candidate = application_dir.filePath(relative_path);
        if (QFileInfo(candidate).isFile()) {
            return candidate;
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString ffmpeg_missing_preview_message() {
    return QStringLiteral("Preview unavailable: ffmpeg executable not found. Put ffmpeg next to CriStudio or on PATH.");
}

bool has_video_decode_error(const QString& stderr_text) {
    const auto lower = stderr_text.toLower();
    return lower.contains(QStringLiteral("failed to read frame header")) ||
           lower.contains(QStringLiteral("failed to read unit")) ||
           lower.contains(QStringLiteral("invalid value")) ||
           lower.contains(QStringLiteral("invalid le value")) ||
           lower.contains(QStringLiteral("invalid data found when processing input")) ||
           lower.contains(QStringLiteral("extra padding at end of superframe")) ||
           lower.contains(QStringLiteral("unexpected ffmpeg behavior")) ||
           lower.contains(QStringLiteral("not all references are available")) ||
           lower.contains(QStringLiteral("error submitting packet to decoder")) ||
           lower.contains(QStringLiteral("error processing packet in decoder")) ||
           lower.contains(QStringLiteral("error while decoding")) ||
           lower.contains(QStringLiteral("could not find codec parameters"));
}

bool validate_video_preview_file(const QString& ffmpeg, const QString& path, QString* error) {
    QProcess ffmpeg_process;
    ffmpeg_process.start(
        ffmpeg,
        QStringList{
            QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"),
            QStringLiteral("error"),
            QStringLiteral("-xerror"),
            QStringLiteral("-y"),
            QStringLiteral("-i"),
            path,
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
    const auto finished = ffmpeg_process.waitForFinished(10000);
    const auto stderr_text = QString::fromLocal8Bit(ffmpeg_process.readAllStandardError()).trimmed();
    if (
        finished &&
        ffmpeg_process.exitStatus() == QProcess::NormalExit &&
        ffmpeg_process.exitCode() == 0 &&
        !has_video_decode_error(stderr_text)
    ) {
        return true;
    }

    if (error != nullptr) {
        *error = stderr_text;
    }
    return false;
}

QString time_text(qint64 milliseconds) {
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const auto total_seconds = milliseconds / 1000;
    if (total_seconds < 10) {
        const auto centiseconds = (milliseconds % 1000) / 10;
        return QStringLiteral("0:%1.%2")
            .arg(total_seconds, 2, 10, QLatin1Char('0'))
            .arg(centiseconds, 2, 10, QLatin1Char('0'));
    }
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void remove_preview_temporary_directory(VideoPreview& video) {
    remove_preview_directory(video);
}

void remove_preview_temporary_directory(MuxPreview& mux) {
    remove_preview_directory(mux);
}




} // namespace cristudio
