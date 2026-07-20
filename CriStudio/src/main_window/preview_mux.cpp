#include "../main_window.hpp"

#include "preview_mux.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace cristudio {

void prepare_mux_preview_for_playback(MuxPreview& mux) {
    if (!mux.playable_path.empty() || mux.video_bytes.empty()) {
        return;
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-mux-preview-XXXXXX"));
    if (!temp_dir.isValid()) {
        mux.note = "could not create temporary mux preview directory";
        return;
    }

    const auto video_suffix = mux.video_suffix.empty()
        ? QStringLiteral(".m2v")
        : utf8_to_qstring(mux.video_suffix);
    const auto video_path = temp_dir.filePath(QStringLiteral("video") + video_suffix);
    QFile video_file(video_path);
    if (!video_file.open(QIODevice::WriteOnly)) {
        mux.note = "could not write temporary mux video stream";
        return;
    }
    video_file.write(reinterpret_cast<const char*>(mux.video_bytes.data()), static_cast<qsizetype>(mux.video_bytes.size()));
    video_file.close();

    QString audio_path;
    if (!mux.audio_wav_bytes.empty()) {
        audio_path = temp_dir.filePath(QStringLiteral("audio.wav"));
        QFile audio_file(audio_path);
        if (!audio_file.open(QIODevice::WriteOnly)) {
            mux.note = "could not write temporary mux audio stream";
            return;
        }
        audio_file.write(reinterpret_cast<const char*>(mux.audio_wav_bytes.data()), static_cast<qsizetype>(mux.audio_wav_bytes.size()));
        audio_file.close();
    }

    QStringList subtitle_paths;
    subtitle_paths.reserve(static_cast<qsizetype>(mux.subtitle_choices.size()));
    for (size_t index = 0; index < mux.subtitle_choices.size(); ++index) {
        const auto& subtitle = mux.subtitle_choices[index];
        const auto subtitle_path = temp_dir.filePath(QStringLiteral("subtitle-%1.srt").arg(index));
        QFile subtitle_file(subtitle_path);
        if (!subtitle_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            mux.note = "could not write temporary mux subtitle stream";
            return;
        }
        subtitle_file.write(subtitle.srt_text.data(), static_cast<qsizetype>(subtitle.srt_text.size()));
        subtitle_file.close();
        subtitle_paths.push_back(subtitle_path);
    }

    const auto ffmpeg = find_ffmpeg_executable();
    if (ffmpeg.isEmpty()) {
        mux.note = ffmpeg_missing_preview_message().toStdString();
        return;
    }

    QStringList input_arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-y"),
    };
    if (mux.frame_rate_n != 0 && mux.frame_rate_d != 0) {
        input_arguments << QStringLiteral("-r") << QStringLiteral("%1/%2").arg(mux.frame_rate_n).arg(mux.frame_rate_d);
    }
    if (!mux.ffmpeg_input_format.empty()) {
        input_arguments << QStringLiteral("-f") << utf8_to_qstring(mux.ffmpeg_input_format);
    }
    input_arguments << QStringLiteral("-i") << video_path;
    if (!audio_path.isEmpty()) {
        input_arguments << QStringLiteral("-i") << audio_path;
    }
    for (const auto& subtitle_path : subtitle_paths) {
        input_arguments << QStringLiteral("-i") << subtitle_path;
    }
    input_arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (!audio_path.isEmpty()) {
        input_arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    }
    const auto subtitle_input_base = audio_path.isEmpty() ? 1 : 2;
    for (int i = 0; i < subtitle_paths.size(); ++i) {
        input_arguments << QStringLiteral("-map") << QStringLiteral("%1:0").arg(subtitle_input_base + i);
    }

    QString mux_error;
    QString playable_path;
    const auto validate_mux_preview = [&](QString const& output_path) {
        if (validate_video_preview_file(ffmpeg, output_path, &mux_error)) {
            return true;
        }
        QFile::remove(output_path);
        return false;
    };

    const auto run_copy_remux = [&](QString const& output_path) {
        QStringList arguments = input_arguments;
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
        if (output_path.endsWith(QStringLiteral(".mov")) || output_path.endsWith(QStringLiteral(".mp4"))) {
            arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
        arguments
            << QStringLiteral("-max_interleave_delta") << QStringLiteral("0")
            << QStringLiteral("-muxdelay") << QStringLiteral("0")
            << QStringLiteral("-muxpreload") << QStringLiteral("0");
        arguments << output_path;

        QProcess ffmpeg_process;
        ffmpeg_process.start(ffmpeg, arguments);
        const auto remux_ok =
            ffmpeg_process.waitForFinished(30000) &&
            ffmpeg_process.exitStatus() == QProcess::NormalExit &&
            ffmpeg_process.exitCode() == 0 &&
            QFileInfo::exists(output_path);
        if (remux_ok && validate_mux_preview(output_path)) {
            playable_path = output_path;
            return true;
        }

        if (!remux_ok) {
            mux_error = QString::fromLocal8Bit(ffmpeg_process.readAllStandardError()).trimmed();
        }
        return false;
    };

    if (playable_path.isEmpty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.mkv")));
    }
    if (playable_path.isEmpty() && subtitle_paths.empty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.mov")));
    }
    if (playable_path.isEmpty() && subtitle_paths.empty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.ts")));
    }

    if (playable_path.isEmpty()) {
        mux.note = video_preview_unavailable_message().toStdString();
        return;
    }

    temp_dir.setAutoRemove(false);
    mux.playable_path = path_from_qstring(playable_path);
    mux.temporary_directory = path_from_qstring(temp_dir.path());
    mux.video_bytes.clear();
    mux.video_bytes.shrink_to_fit();
    mux.audio_wav_bytes.clear();
    mux.audio_wav_bytes.shrink_to_fit();
}
void MainWindow::start_document_mux_preview(const LoadedDocument& document, int audio_choice) {
    if (m_preview_running) {
        ++m_preview_request_id;
        m_pending_preview_entry = std::nullopt;
        m_pending_mux_preview = std::pair{document.path, audio_choice};
        show_preview_document(document);
        show_pending_media_preview(QStringLiteral("Loading mux preview..."));
        return;
    }

    m_pending_mux_preview = std::nullopt;
    if (!has_ffmpeg()) {
        m_current_preview_entry = std::nullopt;
        set_preview_entry_actions_visible(false);
        if (m_toggle_preview_action != nullptr) {
            m_toggle_preview_action->setChecked(true);
        }
        if (m_preview_panel_button != nullptr) {
            m_preview_panel_button->setChecked(true);
        }
        toggle_preview_panel();
        m_nested_title->setText(archive_basename(utf8_to_qstring(document.display_name)));
        m_nested_subtitle->setText(QStringLiteral("Mux preview"));
        populate_info_grid(m_nested_info_grid, document.info);
        update_preview_key_panel(&document);
        reset_audio_preview();
        m_nested_entry_model->clear();
        m_nested_entry_view->hide();
        m_nested_image_scroll->hide();
        show_unavailable_media_preview(ffmpeg_missing_message());
        return;
    }

    m_current_preview_entry = std::nullopt;
    set_preview_entry_actions_visible(false);
    if (m_toggle_preview_action != nullptr) {
        m_toggle_preview_action->setChecked(true);
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->setChecked(true);
    }
    toggle_preview_panel();
    m_nested_title->setText(archive_basename(utf8_to_qstring(document.display_name)));
    m_nested_subtitle->setText(QStringLiteral("Mux preview"));
    populate_info_grid(m_nested_info_grid, document.info);
    update_preview_key_panel(&document);
    reset_audio_preview();
    m_nested_entry_model->clear();
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();
    show_pending_media_preview(QStringLiteral("Loading mux preview..."));
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setCurrentIndex(0);
    }

    const auto request_id = ++m_preview_request_id;
    m_preview_running = true;
    auto keys = m_decryption_keys;
    m_preview_watcher->setFuture(QtConcurrent::run([document, request_id, audio_choice, keys = std::move(keys)] {
        const auto make_result = [request_id, &document, audio_choice](const DecryptionKeys& preview_keys) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            auto mux = build_mux_preview(document, audio_choice, preview_keys);
            if (!mux) {
                result.message = QString::fromStdString(mux.error());
                return result;
            }
            prepare_mux_preview_for_playback(*mux);
            if (mux->playable_path.empty() && !mux->note.empty()) {
                result.message = QString::fromStdString(mux->note);
            }
            result.mux = std::move(*mux);
            return result;
        };

        auto result = make_result(keys);
        if (keys.has_cri_key && (!result.mux || result.mux->playable_path.empty())) {
            auto fallback_keys = keys;
            fallback_keys.has_cri_key = false;
            fallback_keys.cri_key = 0;
            auto fallback_result = make_result(fallback_keys);
            if (fallback_result.mux && !fallback_result.mux->playable_path.empty()) {
                if (result.mux) {
                    remove_preview_temporary_directory(*result.mux);
                }
                result = std::move(fallback_result);
            } else if (fallback_result.mux) {
                remove_preview_temporary_directory(*fallback_result.mux);
            }
        }
        return result;
    }));
}

void MainWindow::configure_mux_preview(const MuxPreview& mux) {
    reset_audio_preview();
    m_video_temp_dir = mux.temporary_directory;
    if (!ensure_media_backend()) {
        show_unavailable_media_preview(QStringLiteral("Mux preview backend is unavailable"));
        return;
    }
    if (m_video_widget == nullptr) {
        return;
    }

    if (m_mux_audio_combo != nullptr) {
        QSignalBlocker blocker(m_mux_audio_combo);
        m_mux_audio_combo->clear();
        for (int i = 0; i < static_cast<int>(mux.audio_choices.size()); ++i) {
            const auto& choice = mux.audio_choices[static_cast<size_t>(i)];
            auto label = archive_basename(strip_mux_prefix(utf8_to_qstring(choice.name)));
            if (!choice.detail.empty()) {
                label += QStringLiteral("  -  ") + utf8_to_qstring(choice.detail);
            }
            m_mux_audio_combo->addItem(label, i);
        }
        if (mux.selected_audio >= 0 && mux.selected_audio < m_mux_audio_combo->count()) {
            m_mux_audio_combo->setCurrentIndex(mux.selected_audio);
        }
    }
    if (m_mux_audio_row != nullptr) {
        m_mux_audio_row->setVisible(m_mux_audio_combo != nullptr && m_mux_audio_combo->count() > 0);
    }
    if (m_mux_subtitle_combo != nullptr) {
        QSignalBlocker blocker(m_mux_subtitle_combo);
        m_mux_subtitle_combo->clear();
        m_mux_subtitle_combo->addItem(QStringLiteral("Disabled"), -1);
        for (int i = 0; i < static_cast<int>(mux.subtitle_choices.size()); ++i) {
            const auto& choice = mux.subtitle_choices[static_cast<size_t>(i)];
            auto label = utf8_to_qstring(choice.detail.empty() ? choice.name : choice.detail);
            if (!choice.name.empty()) {
                label += QStringLiteral("  -  ") + archive_basename(strip_mux_prefix(utf8_to_qstring(choice.name)));
            }
            m_mux_subtitle_combo->addItem(label, i);
        }
        if (mux.selected_subtitle >= 0 && mux.selected_subtitle + 1 < m_mux_subtitle_combo->count()) {
            m_mux_subtitle_combo->setCurrentIndex(mux.selected_subtitle + 1);
        } else {
            m_mux_subtitle_combo->setCurrentIndex(0);
        }
    }
    if (m_mux_subtitle_row != nullptr) {
        m_mux_subtitle_row->setVisible(!mux.subtitle_choices.empty());
    }

    if (mux.playable_path.empty()) {
        show_unavailable_media_preview(mux.note.empty()
            ? QStringLiteral("Mux preview is unavailable")
            : utf8_to_qstring(mux.note));
        return;
    }

    m_audio_source_path = to_qstring(mux.playable_path);
    m_preview_duration_ms = static_cast<qint64>(std::min<uint64_t>(
        mux.duration_ms,
        static_cast<uint64_t>(std::numeric_limits<qint64>::max())
    ));
    if (m_preview_duration_ms > 0) {
        m_audio_progress->setRange(0, static_cast<int>(std::clamp<qint64>(
            m_preview_duration_ms,
            0,
            std::numeric_limits<int>::max()
        )));
    }

    m_audio_player->setVideoOutput(m_video_widget);
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    if (m_mux_subtitle_combo != nullptr && m_mux_subtitle_combo->currentIndex() >= 0) {
        m_audio_player->setActiveSubtitleTrack(m_mux_subtitle_combo->currentData().toInt());
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
    auto label = QStringLiteral("Mux preview - ") + utf8_to_qstring(mux.format);
    if (!mux.audio_label.empty()) {
        label += QStringLiteral(" + ") + archive_basename(strip_mux_prefix(utf8_to_qstring(mux.audio_label)));
    } else {
        label += QStringLiteral(" (video only)");
    }
    m_audio_status_label->setText(label);
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



} // namespace cristudio
