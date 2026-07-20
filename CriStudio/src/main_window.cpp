#include "main_window.hpp"

#include "browser/browser_delegates.hpp"
#include "editor_workspace.hpp"
#include "main_window/preview_helpers.hpp"
#include "main_window/ui_helpers.hpp"
#include "path_text.hpp"
#include "adx_crypto.hpp"
#include "cvm_crypto.hpp"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEasingCurve>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <system_error>
#include <utility>

namespace cristudio {
namespace {

bool system_prefers_dark_theme() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints(); hints != nullptr) {
        const auto scheme = hints->colorScheme();
        if (scheme == Qt::ColorScheme::Dark) {
            return true;
        }
        if (scheme == Qt::ColorScheme::Light) {
            return false;
        }
    }
#endif
#if defined(Q_OS_WIN) || defined(_WIN32)
    QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    m_theme = system_prefers_dark_theme() ? Theme::Dark : Theme::Light;
    build_ui();
    build_menus();
    set_theme(m_theme);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints(); hints != nullptr) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
            if (scheme == Qt::ColorScheme::Dark) {
                set_theme(Theme::Dark);
            } else if (scheme == Qt::ColorScheme::Light) {
                set_theme(Theme::Light);
            }
        });
    }
#endif
    setAcceptDrops(true);
    resize(1200, 760);
    setWindowTitle(app_title());
    restore_ui_state();
    schedule_position_edge_buttons();

    const auto path = log_path();
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_log_file.setFileName(path);
    if (m_log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        append_log(QStringLiteral("CriStudio started"));
    }
}

MainWindow::~MainWindow() {
    save_ui_state();
    if (auto* app = QApplication::instance(); app != nullptr) {
        app->removeEventFilter(this);
    }
    if (m_audio_player != nullptr) {
        m_audio_player->setVideoOutput(nullptr);
        m_audio_player->stop();
        m_audio_player->setSource({});
    }
    if (!m_video_temp_dir.empty()) {
        std::error_code remove_error;
        std::filesystem::remove_all(m_video_temp_dir, remove_error);
        m_video_temp_dir.clear();
    }
    for (const auto& temp_dir : m_deferred_video_temp_dirs) {
        std::error_code remove_error;
        std::filesystem::remove_all(temp_dir, remove_error);
    }
    m_deferred_video_temp_dirs.clear();
}

void MainWindow::restore_ui_state() {
    QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    settings.beginGroup(QStringLiteral("ui"));
    if (const auto geometry = settings.value(QStringLiteral("geometry")).toByteArray(); !geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (const auto state = settings.value(QStringLiteral("browserSplitter")).toByteArray(); !state.isEmpty() && m_splitter != nullptr) {
        m_splitter->restoreState(state);
    }
    if (const auto state = settings.value(QStringLiteral("entryHeader")).toByteArray(); !state.isEmpty() && m_entry_view != nullptr) {
        m_entry_view->header()->restoreState(state);
    }
    if (const auto state = settings.value(QStringLiteral("previewHeader")).toByteArray(); !state.isEmpty() && m_nested_entry_view != nullptr) {
        m_nested_entry_view->header()->restoreState(state);
    }

    const auto entry_mode = std::clamp(settings.value(QStringLiteral("entryViewMode"), 0).toInt(), 0, 1);
    if (m_entry_view_mode != nullptr) {
        m_entry_view_mode->setCurrentIndex(entry_mode);
    }
    if (m_file_sort != nullptr) {
        m_file_sort->setCurrentIndex(std::clamp(settings.value(QStringLiteral("fileSort"), 0).toInt(), 0, m_file_sort->count() - 1));
    }
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentIndex(std::clamp(settings.value(QStringLiteral("workspace"), 0).toInt(), 0, m_workspace_tabs->count() - 1));
    }

    const auto left_visible = settings.value(QStringLiteral("leftPanelVisible"), true).toBool();
    if (m_toggle_left_action != nullptr && m_left_panel_button != nullptr) {
        m_toggle_left_action->setChecked(left_visible);
        m_left_panel_button->setChecked(left_visible);
        toggle_left_panel();
    }
    const auto preview_visible = settings.value(QStringLiteral("previewPanelVisible"), false).toBool();
    if (m_toggle_preview_action != nullptr && m_preview_panel_button != nullptr) {
        m_toggle_preview_action->setChecked(preview_visible);
        m_preview_panel_button->setChecked(preview_visible);
        toggle_preview_panel();
    }
    if (const auto state = settings.value(QStringLiteral("browserSplitter")).toByteArray(); !state.isEmpty() && m_splitter != nullptr) {
        m_splitter->restoreState(state);
    }

    if (settings.contains(QStringLiteral("theme"))) {
        const auto theme = settings.value(QStringLiteral("theme")).toInt() == static_cast<int>(Theme::Dark)
            ? Theme::Dark
            : Theme::Light;
        set_theme(theme);
    }
    settings.endGroup();
}

void MainWindow::save_ui_state() const {
    QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    settings.beginGroup(QStringLiteral("ui"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    if (m_splitter != nullptr) {
        settings.setValue(QStringLiteral("browserSplitter"), m_splitter->saveState());
    }
    if (m_entry_view != nullptr) {
        settings.setValue(QStringLiteral("entryHeader"), m_entry_view->header()->saveState());
    }
    if (m_nested_entry_view != nullptr) {
        settings.setValue(QStringLiteral("previewHeader"), m_nested_entry_view->header()->saveState());
    }
    settings.setValue(QStringLiteral("entryViewMode"), m_entry_view_mode == nullptr ? 0 : m_entry_view_mode->currentIndex());
    settings.setValue(QStringLiteral("fileSort"), m_file_sort == nullptr ? 0 : m_file_sort->currentIndex());
    settings.setValue(QStringLiteral("workspace"), m_workspace_tabs == nullptr ? 0 : m_workspace_tabs->currentIndex());
    settings.setValue(QStringLiteral("leftPanelVisible"), m_left_panel_button != nullptr && m_left_panel_button->isChecked());
    settings.setValue(QStringLiteral("previewPanelVisible"), m_preview_panel_button != nullptr && m_preview_panel_button->isChecked());
    settings.setValue(QStringLiteral("theme"), static_cast<int>(m_theme));
    settings.setValue(
        QStringLiteral("alwaysShowAccessKeys"),
        m_always_show_access_keys_action != nullptr && m_always_show_access_keys_action->isChecked()
    );
    settings.setValue(QStringLiteral("compactLists"), m_compact_lists_action != nullptr && m_compact_lists_action->isChecked());
    settings.endGroup();
}

void MainWindow::set_compact_lists(bool compact) {
    for (auto* view : {static_cast<QAbstractItemView*>(m_file_view), static_cast<QAbstractItemView*>(m_entry_view), static_cast<QAbstractItemView*>(m_nested_entry_view)}) {
        if (view == nullptr) {
            continue;
        }
        if (auto* delegate = dynamic_cast<LoadedFileDelegate*>(view->itemDelegate()); delegate != nullptr) {
            delegate->set_compact(compact);
        }
        if (auto* delegate = dynamic_cast<EntryTreeDelegate*>(view->itemDelegate()); delegate != nullptr) {
            delegate->set_compact(compact);
        }
        view->doItemsLayout();
        view->viewport()->update();
    }
}

void MainWindow::load_startup_paths(std::vector<std::filesystem::path> paths) {
    start_loading_paths(std::move(paths));
}

void MainWindow::open_scratch_utf_editor() {
    new_utf_editor_document();
}

bool MainWindow::has_background_work() const {
    return m_load_running ||
        m_preview_running ||
        m_extract_running ||
        m_hca_key_recovery_running ||
        m_usm_key_recovery_running ||
        m_adx_key_recovery_running ||
        m_aac_key_recovery_running ||
        m_materialize_running ||
        (m_editor_workspace != nullptr && m_editor_workspace->has_background_work());
}

bool MainWindow::reload_current_document_with_keys() {
    if (m_file_view == nullptr || !m_file_view->currentIndex().isValid()) {
        return false;
    }

    const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
    const auto source_row = source.row();
    const auto canonical = m_file_model->canonical_path_at(source_row);
    if (canonical.isEmpty()) {
        return false;
    }

    std::string reason;
    auto document = load_document_summary(path_from_qstring(canonical), reason, m_decryption_keys);
    if (!document) {
        statusBar()->showMessage(QStringLiteral("Reload with key failed: ") + utf8_to_qstring(reason), 6000);
        append_log(QStringLiteral("Reload with key failed: ") + canonical + QStringLiteral(" (") + utf8_to_qstring(reason) + QStringLiteral(")"));
        return false;
    }

    m_file_model->replace_document(source_row, std::move(*document));
    const auto current = m_file_proxy->mapFromSource(m_file_model->index(source_row, 0));
    if (current.isValid()) {
        m_file_view->setCurrentIndex(current);
    }
    show_document(m_file_model->document_at(source_row));
    statusBar()->showMessage(QStringLiteral("Reloaded selected file with current keys"), 3000);
    return true;
}

const LoadedDocument* MainWindow::ensure_loaded_document(int row) {
    if (m_file_model == nullptr) {
        return nullptr;
    }
    const auto* current = m_file_model->document_at(row);
    if (current == nullptr || current->summary_loaded) {
        return current;
    }

    start_document_materialization(row);
    return current;
}

void MainWindow::select_document(int row) {
    if (m_file_model == nullptr) {
        show_document(nullptr);
        return;
    }

    const auto* document = m_file_model->document_at(row);
    show_document(document);
    if (document == nullptr || document->summary_loaded) {
        return;
    }

    start_document_materialization(row);
}

bool MainWindow::ensure_media_backend() {
    if (m_audio_player != nullptr && m_audio_output != nullptr) {
        return true;
    }

    m_audio_player = new QMediaPlayer(this);
    m_audio_output = new QAudioOutput(this);
    const auto volume = m_audio_volume_slider == nullptr ? 80 : std::clamp(m_audio_volume_slider->value(), 0, 100);
    m_audio_output->setVolume(static_cast<float>(volume) / 100.0f);
    m_audio_player->setAudioOutput(m_audio_output);

    connect(m_audio_player, &QMediaPlayer::tracksChanged, this, [this] {
        if (
            m_audio_player == nullptr ||
            m_mux_subtitle_row == nullptr ||
            m_mux_subtitle_combo == nullptr ||
            !m_mux_subtitle_row->isVisible() ||
            m_mux_subtitle_combo->currentIndex() < 0
        ) {
            return;
        }
        m_audio_player->setActiveSubtitleTrack(m_mux_subtitle_combo->currentData().toInt());
    });
    connect(m_audio_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        const auto effective_duration = m_preview_duration_ms > 0 ? m_preview_duration_ms : duration;
        const auto safe_duration = std::clamp<qint64>(effective_duration, 0, std::numeric_limits<int>::max());
        m_audio_progress->setRange(0, static_cast<int>(safe_duration));
        update_audio_time_label();
    });
    connect(m_audio_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!m_audio_slider_dragging) {
            QSignalBlocker blocker(m_audio_progress);
            m_audio_progress->setValue(static_cast<int>(std::clamp<qint64>(position, 0, std::numeric_limits<int>::max())));
        }
        handle_loop_position(position);
        update_audio_time_label();
    });
    connect(m_audio_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            const auto end_position = m_preview_duration_ms > 0
                ? m_preview_duration_ms
                : (m_audio_player == nullptr ? 0 : m_audio_player->duration());
            handle_loop_position(end_position);
        }
    });
    connect(m_audio_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        const auto playing = state == QMediaPlayer::PlayingState;
        m_audio_play_button->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        m_audio_play_button->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    });
    connect(m_audio_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& error) {
        if (m_audio_status_label != nullptr && !error.isEmpty()) {
            m_audio_status_label->setText(QStringLiteral("Playback error: ") + error);
        }
    });
    return true;
}

QString MainWindow::ffmpeg_executable_path() {
    if (!m_ffmpeg_path_checked) {
        m_ffmpeg_executable = find_ffmpeg_executable();
        m_ffmpeg_path_checked = true;
    }
    return m_ffmpeg_executable;
}

bool MainWindow::has_ffmpeg() {
    return !ffmpeg_executable_path().isEmpty();
}

QString MainWindow::ffmpeg_missing_message() const {
    return QStringLiteral("Preview unavailable: ffmpeg executable not found. Put ffmpeg next to CriStudio or on PATH.");
}

void MainWindow::append_log(const QString& message) {
    if (!m_log_file.isOpen()) {
        return;
    }

    QTextStream stream(&m_log_file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << " " << message << "\n";
    m_log_file.flush();
}

QString MainWindow::log_path() const {
    auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/.cristudio");
    }
    return root + QStringLiteral("/cristudio.log");
}

} // namespace cristudio
