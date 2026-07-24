#include "../main_window.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"

#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVideoWidget>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <limits>
#include <typeinfo>
#include <utility>

namespace cristudio {

void MainWindow::start_document_audio_preview(const LoadedDocument& document) {
    if (m_preview_running) {
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
    show_preview_document(document);
    if (!is_direct_audio_document(document)) {
        return;
    }

    const auto request_id = m_preview_request_id;
    show_pending_media_preview(QStringLiteral("Loading audio preview..."));
    append_log(QStringLiteral("Audio preview started [%1]: %2")
        .arg(request_id)
        .arg(path_to_qstring(document.path)));

    m_preview_running = true;
    auto keys = m_decryption_keys;
    m_preview_watcher->setFuture(QtConcurrent::run([document, request_id, keys = std::move(keys)] {
        const auto stage = QStringLiteral("extracting and decoding the audio stream");
        try {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            if (auto audio = build_audio_preview(document, keys); audio) {
                result.audio = std::move(*audio);
            } else {
                result.message = utf8_to_qstring(audio.error());
            }
            return result;
        } catch (const std::exception& error) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QStringLiteral("Audio preview failed while %1: %2 [%3]")
                .arg(
                    stage,
                    QString::fromLocal8Bit(error.what()),
                    QString::fromLatin1(typeid(error).name())
                );
            return result;
        } catch (...) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QStringLiteral("Audio preview failed while %1 with an unknown exception").arg(stage);
            return result;
        }
    }));
}

void MainWindow::configure_audio_preview(const AudioPreview& audio) {
    reset_audio_preview();
    if (!ensure_media_backend()) {
        show_unavailable_media_preview(QStringLiteral("Audio preview backend is unavailable"));
        return;
    }
    if (m_video_widget != nullptr) {
        m_video_widget->hide();
    }
    if (m_video_container != nullptr) {
        m_video_container->hide();
    }
    m_audio_sample_count = audio.sample_count;
    m_audio_sample_rate = audio.sample_rate;
    m_audio_loops = audio.loops;

    if (!audio.playable_path.empty()) {
        m_audio_source_path = to_qstring(audio.playable_path);
    } else if (!audio.wav_bytes.empty()) {
        m_audio_temp_dir = std::make_unique<QTemporaryDir>();
        if (!m_audio_temp_dir->isValid()) {
            m_audio_status_label->setText(QStringLiteral("Could not create temporary playback directory"));
            fade_widget_in(m_audio_panel);
            return;
        }

        const auto output_path = m_audio_temp_dir->filePath(QStringLiteral("preview.wav"));
        QFile output(output_path);
        if (!output.open(QIODevice::WriteOnly)) {
            m_audio_status_label->setText(QStringLiteral("Could not write temporary WAV preview"));
            fade_widget_in(m_audio_panel);
            return;
        }
        output.write(reinterpret_cast<const char*>(audio.wav_bytes.data()), static_cast<qsizetype>(audio.wav_bytes.size()));
        output.close();
        m_audio_source_path = output_path;
    }

    if (m_audio_source_path.isEmpty()) {
        m_audio_status_label->setText(QStringLiteral("Audio preview is unavailable"));
        fade_widget_in(m_audio_panel);
        return;
    }

    const auto duration_ms = audio.sample_rate == 0
        ? 0
        : static_cast<qint64>((audio.sample_count * 1000ull) / audio.sample_rate);
    m_preview_duration_ms = duration_ms;
    m_audio_progress->setRange(0, static_cast<int>(std::clamp<qint64>(duration_ms, 0, std::numeric_limits<int>::max())));
    m_audio_progress->setValue(0);
    m_audio_play_button->setEnabled(true);
    m_audio_progress->setEnabled(true);
    if (m_audio_volume_label != nullptr) {
        m_audio_volume_label->show();
    }
    if (m_audio_volume_slider != nullptr) {
        m_audio_volume_slider->show();
        m_audio_volume_slider->setEnabled(true);
    }
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    m_audio_status_label->setText(QStringLiteral("%1 - %2 ch, %3 Hz")
        .arg(utf8_to_qstring(audio.format))
        .arg(audio.channels)
        .arg(audio.sample_rate));
    update_loop_controls(audio);
    update_audio_time_label();
    fade_widget_in(m_audio_panel);
    m_nested_body->hide();
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setCurrentIndex(0);
    }
}

void MainWindow::reset_audio_preview() {
    const auto had_video_preview = m_video_preview_active;
    if (m_audio_player != nullptr) {
        m_audio_player->setVideoOutput(nullptr);
        m_audio_player->stop();
        m_audio_player->setSource({});
    }
    if (m_video_widget != nullptr) {
        m_video_widget->hide();
        m_video_widget->repaint();
    }
    if (m_video_container != nullptr) {
        m_video_container->hide();
        m_video_container->repaint();
    }
    m_audio_source_path.clear();
    m_audio_temp_dir.reset();
    release_video_preview_resources();
    m_audio_sample_count = 0;
    m_audio_sample_rate = 0;
    m_audio_loops.clear();
    m_preview_duration_ms = 0;
    m_audio_slider_dragging = false;
    m_audio_resume_after_seek = false;
    m_audio_loop_seeking = false;
    m_video_preview_active = false;
    if (m_audio_play_button != nullptr) {
        m_audio_play_button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_audio_play_button->setText(QStringLiteral("Play"));
        m_audio_play_button->setEnabled(false);
    }
    if (m_audio_progress != nullptr) {
        m_audio_progress->setRange(0, 0);
        m_audio_progress->setValue(0);
        m_audio_progress->setEnabled(false);
    }
    if (m_audio_time_label != nullptr) {
        m_audio_time_label->setText(QStringLiteral("0:00 / 0:00"));
    }
    if (m_audio_volume_label != nullptr) {
        m_audio_volume_label->hide();
    }
    if (m_audio_volume_slider != nullptr) {
        m_audio_volume_slider->hide();
        m_audio_volume_slider->setEnabled(false);
    }
    if (m_audio_status_label != nullptr) {
        m_audio_status_label->setText(QStringLiteral("No playable audio selected"));
    }
    if (m_audio_loop_toggle != nullptr) {
        QSignalBlocker blocker(m_audio_loop_toggle);
        m_audio_loop_toggle->setChecked(false);
        m_audio_loop_toggle->setEnabled(false);
    }
    if (m_audio_loop_list != nullptr) {
        QSignalBlocker blocker(m_audio_loop_list);
        m_audio_loop_list->clear();
        m_audio_loop_list->setEnabled(false);
    }
    if (m_audio_loop_row != nullptr) {
        m_audio_loop_row->hide();
    }
    if (m_mux_audio_combo != nullptr) {
        QSignalBlocker blocker(m_mux_audio_combo);
        m_mux_audio_combo->clear();
    }
    if (m_mux_audio_row != nullptr) {
        m_mux_audio_row->hide();
    }
    if (m_mux_subtitle_combo != nullptr) {
        QSignalBlocker blocker(m_mux_subtitle_combo);
        m_mux_subtitle_combo->clear();
    }
    if (m_mux_subtitle_row != nullptr) {
        m_mux_subtitle_row->hide();
    }
    if (m_audio_panel != nullptr) {
        m_audio_panel->hide();
    }
    if (had_video_preview) {
        recreate_video_widget();
    }
}

void MainWindow::update_audio_time_label() {
    if (m_audio_player == nullptr || m_audio_time_label == nullptr) {
        return;
    }

    auto duration = m_audio_player->duration();
    if (m_preview_duration_ms > 0) {
        duration = m_preview_duration_ms;
    } else if (duration <= 0 && m_audio_sample_rate != 0) {
        duration = static_cast<qint64>((m_audio_sample_count * 1000ull) / m_audio_sample_rate);
    }
    const auto position = m_audio_slider_dragging && m_audio_progress != nullptr
        ? static_cast<qint64>(m_audio_progress->value())
        : m_audio_player->position();
    m_audio_time_label->setText(time_text(position) + QStringLiteral(" / ") + time_text(duration));
}

void MainWindow::update_loop_controls(const AudioPreview& audio) {
    if (m_audio_loop_toggle == nullptr || m_audio_loop_list == nullptr) {
        return;
    }

    QSignalBlocker toggle_blocker(m_audio_loop_toggle);
    QSignalBlocker list_blocker(m_audio_loop_list);
    m_audio_loop_list->clear();

    const auto sample_rate = audio.sample_rate;
    const auto to_ms = [sample_rate](uint64_t sample) -> qint64 {
        if (sample_rate == 0) {
            return 0;
        }
        return static_cast<qint64>((sample * 1000ull) / sample_rate);
    };

    for (size_t i = 0; i < m_audio_loops.size(); ++i) {
        const auto& loop = m_audio_loops[i];
        auto name = utf8_to_qstring(loop.name);
        if (const auto bracket = name.indexOf(QStringLiteral(" [")); bracket > 0) {
            name = name.left(bracket);
        }
        const auto label = QStringLiteral("%1    %2 - %3    samples %4 - %5")
            .arg(name)
            .arg(time_text(to_ms(loop.start_sample)))
            .arg(time_text(to_ms(loop.end_sample)))
            .arg(loop.start_sample)
            .arg(loop.end_sample);
        auto* item = new QListWidgetItem(label, m_audio_loop_list);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setSizeHint(QSize(0, 30));
        item->setToolTip(QStringLiteral("%1: samples %2 - %3, time %4 - %5")
            .arg(name)
            .arg(loop.start_sample)
            .arg(loop.end_sample)
            .arg(time_text(to_ms(loop.start_sample)))
            .arg(time_text(to_ms(loop.end_sample))));
    }

    const auto has_loops = !m_audio_loops.empty() && sample_rate != 0;
    if (m_audio_loop_row != nullptr) {
        m_audio_loop_row->setVisible(has_loops);
    }
    m_audio_loop_toggle->setEnabled(has_loops);
    m_audio_loop_toggle->setChecked(false);
    m_audio_loop_list->setEnabled(has_loops);
    if (has_loops) {
        const auto visible_rows = std::min<int>(4, static_cast<int>(m_audio_loops.size()));
        m_audio_loop_list->setFixedHeight(std::max(36, 32 * visible_rows + 6));
        m_audio_loop_list->setCurrentRow(0);
    }
}

void MainWindow::handle_loop_position(qint64 position) {
    if (
        m_audio_player == nullptr ||
        m_audio_loop_toggle == nullptr ||
        m_audio_loop_list == nullptr ||
        !m_audio_loop_toggle->isChecked() ||
        m_audio_loop_seeking ||
        m_audio_slider_dragging ||
        m_audio_sample_rate == 0
    ) {
        return;
    }

    const auto loop_index = m_audio_loop_list->currentRow();
    if (loop_index < 0 || loop_index >= static_cast<int>(m_audio_loops.size())) {
        return;
    }

    const auto& loop = m_audio_loops[static_cast<size_t>(loop_index)];
    const auto start_ms = static_cast<qint64>((loop.start_sample * 1000ull) / m_audio_sample_rate);
    const auto end_ms = static_cast<qint64>((loop.end_sample * 1000ull) / m_audio_sample_rate);
    if (end_ms <= start_ms || position < end_ms) {
        return;
    }

    m_audio_loop_seeking = true;
    m_audio_player->setPosition(start_ms);
    if (m_audio_player->playbackState() != QMediaPlayer::PlayingState && !m_audio_source_path.isEmpty()) {
        m_audio_player->play();
    }
    m_audio_loop_seeking = false;
}



} // namespace cristudio
