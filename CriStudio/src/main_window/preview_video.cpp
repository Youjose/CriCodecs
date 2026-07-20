#include "../main_window.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"

#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace cristudio {

void prepare_video_preview_for_playback(VideoPreview& video) {
    if (!video.playable_path.empty() || video.video_bytes.empty()) {
        return;
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-preview-XXXXXX"));
    if (!temp_dir.isValid()) {
        return;
    }

    const auto suffix = video.file_suffix.empty()
        ? QStringLiteral(".m2v")
        : utf8_to_qstring(video.file_suffix);
    const auto raw_path = temp_dir.filePath(QStringLiteral("preview") + suffix);
    QFile output(raw_path);
    if (!output.open(QIODevice::WriteOnly)) {
        return;
    }
    output.write(reinterpret_cast<const char*>(video.video_bytes.data()), static_cast<qsizetype>(video.video_bytes.size()));
    output.close();

    QString playback_path = raw_path;
    const auto ffmpeg = find_ffmpeg_executable();
    QString validation_error;
    if (
        video.remux_for_playback &&
        !video.ffmpeg_input_format.empty() &&
        video.frame_rate_n != 0 &&
        video.frame_rate_d != 0
    ) {
        if (!ffmpeg.isEmpty()) {
            const auto input_format = utf8_to_qstring(video.ffmpeg_input_format);
            const auto frame_rate = QStringLiteral("%1/%2").arg(video.frame_rate_n).arg(video.frame_rate_d);
            const auto output_suffix = input_format == QStringLiteral("h264")
                ? QStringLiteral(".mp4")
                : QStringLiteral(".ts");
            const auto remuxed_path = temp_dir.filePath(QStringLiteral("preview-remuxed") + output_suffix);
            QStringList arguments{
                QStringLiteral("-hide_banner"),
                QStringLiteral("-loglevel"),
                QStringLiteral("error"),
                QStringLiteral("-y"),
                QStringLiteral("-r"),
                frame_rate,
                QStringLiteral("-f"),
                input_format,
                QStringLiteral("-i"),
                raw_path,
                QStringLiteral("-map"),
                QStringLiteral("0:v:0"),
                QStringLiteral("-c:v"),
                QStringLiteral("copy"),
            };
            if (output_suffix == QStringLiteral(".mp4")) {
                arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            }
            arguments << remuxed_path;

            QProcess ffmpeg_process;
            ffmpeg_process.start(ffmpeg, arguments);
            if (ffmpeg_process.waitForFinished(30000) &&
                ffmpeg_process.exitStatus() == QProcess::NormalExit &&
                ffmpeg_process.exitCode() == 0 &&
                QFileInfo::exists(remuxed_path)) {
                playback_path = remuxed_path;
                video.format += " - timestamped preview";
            } else {
                validation_error = QString::fromLocal8Bit(ffmpeg_process.readAllStandardError()).trimmed();
            }
        } else {
            video.note = ffmpeg_missing_preview_message().toStdString();
            video.video_bytes.clear();
            video.video_bytes.shrink_to_fit();
            return;
        }
    }

    if (!ffmpeg.isEmpty() && !validate_video_preview_file(ffmpeg, playback_path, &validation_error)) {
        video.note = video_preview_unavailable_message().toStdString();
        video.video_bytes.clear();
        video.video_bytes.shrink_to_fit();
        return;
    }

    temp_dir.setAutoRemove(false);
    video.playable_path = path_from_qstring(playback_path);
    video.temporary_directory = path_from_qstring(temp_dir.path());
    video.video_bytes.clear();
    video.video_bytes.shrink_to_fit();
}



void MainWindow::configure_video_preview(const VideoPreview& video) {
    reset_audio_preview();
    m_video_temp_dir = video.temporary_directory;
    if (!ensure_media_backend()) {
        show_unavailable_media_preview(QStringLiteral("Video preview backend is unavailable"));
        return;
    }
    if (m_video_widget == nullptr) {
        return;
    }

    auto playback_note = utf8_to_qstring(video.format);
    m_preview_duration_ms = static_cast<qint64>(std::min<uint64_t>(
        video.duration_ms,
        static_cast<uint64_t>(std::numeric_limits<qint64>::max())
    ));

    if (!video.playable_path.empty()) {
        m_audio_source_path = to_qstring(video.playable_path);
    } else if (!video.video_bytes.empty()) {
        m_audio_temp_dir = std::make_unique<QTemporaryDir>();
        if (!m_audio_temp_dir->isValid()) {
            m_audio_status_label->setText(QStringLiteral("Could not create temporary video preview directory"));
            fade_widget_in(m_audio_panel);
            return;
        }

        const auto suffix = video.file_suffix.empty()
            ? QStringLiteral(".m2v")
            : utf8_to_qstring(video.file_suffix);
        const auto output_path = m_audio_temp_dir->filePath(QStringLiteral("preview") + suffix);
        QFile output(output_path);
        if (!output.open(QIODevice::WriteOnly)) {
            m_audio_status_label->setText(QStringLiteral("Could not write temporary video preview"));
            fade_widget_in(m_audio_panel);
            return;
        }
        output.write(reinterpret_cast<const char*>(video.video_bytes.data()), static_cast<qsizetype>(video.video_bytes.size()));
        output.close();
        m_audio_source_path = output_path;
        if (video.remux_for_playback) {
            playback_note += QStringLiteral(" - raw stream preview");
        }
    }

    if (m_audio_source_path.isEmpty()) {
        m_audio_status_label->setText(video.note.empty()
            ? QStringLiteral("Video preview is unavailable")
            : utf8_to_qstring(video.note));
        fade_widget_in(m_audio_panel);
        return;
    }

    m_audio_player->setVideoOutput(m_video_widget);
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    if (m_preview_duration_ms > 0) {
        m_audio_progress->setRange(0, static_cast<int>(std::clamp<qint64>(
            m_preview_duration_ms,
            0,
            std::numeric_limits<int>::max()
        )));
    }
    m_video_preview_active = true;
    m_audio_play_button->setEnabled(true);
    m_audio_progress->setEnabled(true);
    if (m_audio_volume_label != nullptr) {
        m_audio_volume_label->show();
    }
    if (m_audio_volume_slider != nullptr) {
        m_audio_volume_slider->show();
        m_audio_volume_slider->setEnabled(true);
    }
    m_audio_status_label->setText(playback_note);
    update_audio_time_label();
    if (m_video_container != nullptr) {
        m_video_container->show();
    }
    m_video_widget->show();
    fade_widget_in(m_audio_panel);
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();
    m_nested_body->hide();
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setCurrentIndex(0);
    }
}

void MainWindow::release_video_preview_resources() {
    if (m_video_temp_dir.empty()) {
        return;
    }

    auto temp_dir = std::move(m_video_temp_dir);
    m_video_temp_dir.clear();
    m_deferred_video_temp_dirs.push_back(temp_dir);
    QTimer::singleShot(2000, this, [this, temp_dir = std::move(temp_dir)] {
        std::error_code remove_error;
        std::filesystem::remove_all(temp_dir, remove_error);
        std::erase(m_deferred_video_temp_dirs, temp_dir);
    });
}

void MainWindow::recreate_video_widget() {
    if (m_video_widget == nullptr || m_video_container == nullptr) {
        return;
    }

    auto* old_widget = m_video_widget;
    auto* box = qobject_cast<QVBoxLayout*>(m_video_container->layout());
    const auto index = box != nullptr ? box->indexOf(old_widget) : -1;

    old_widget->hide();
    if (box != nullptr) {
        box->removeWidget(old_widget);
    }
    old_widget->deleteLater();

    m_video_widget = new QVideoWidget(m_video_container);
    m_video_widget->setMinimumHeight(260);
    m_video_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_video_widget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_video_widget->hide();
    if (box != nullptr && index >= 0) {
        box->insertWidget(index, m_video_widget);
    }
}

void MainWindow::set_preview_image(const QImage& image) {
    m_nested_source_pixmap = QPixmap::fromImage(image);
    update_preview_image();
}

void MainWindow::update_preview_image() {
    if (m_nested_image_scroll == nullptr || m_nested_image == nullptr || m_nested_source_pixmap.isNull()) {
        return;
    }

    const auto viewport_size = m_nested_image_scroll->viewport() != nullptr
        ? m_nested_image_scroll->viewport()->size()
        : m_nested_image_scroll->size();
    if (viewport_size.isEmpty()) {
        return;
    }

    auto shown = m_nested_source_pixmap;
    if (shown.width() > viewport_size.width() || shown.height() > viewport_size.height()) {
        shown = m_nested_source_pixmap.scaled(
            viewport_size,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }

    m_nested_image->setPixmap(shown);
    m_nested_image->setMinimumSize(shown.size());
    m_nested_image->setAlignment(Qt::AlignCenter);
}



} // namespace cristudio
