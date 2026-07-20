#include "../main_window.hpp"

#include "../editor_workspace.hpp"
#include "../shared/document_preview_router.hpp"
#include "../path_text.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMimeData>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeView>

#include <utility>

namespace cristudio {

namespace {

bool editor_workspace_active(const QTabWidget* tabs, const EditorWorkspace* editor) {
    return tabs != nullptr && editor != nullptr && tabs->currentWidget() == editor;
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        set_drop_overlay_visible(true);
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    set_drop_overlay_visible(false);
    if (editor_workspace_active(m_workspace_tabs, m_editor_workspace)) {
        open_urls_in_editor(event->mimeData()->urls());
    } else {
        add_paths(event->mimeData()->urls());
    }
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto* drag_event = static_cast<QDragMoveEvent*>(event);
        if (drag_event->mimeData()->hasUrls()) {
            set_drop_overlay_visible(true);
            drag_event->acceptProposedAction();
            return true;
        }
    }
    if (event->type() == QEvent::DragLeave) {
        set_drop_overlay_visible(false);
        return false;
    }
    if (event->type() == QEvent::Drop) {
        auto* drop_event = static_cast<QDropEvent*>(event);
        if (drop_event->mimeData()->hasUrls()) {
            set_drop_overlay_visible(false);
            if (editor_workspace_active(m_workspace_tabs, m_editor_workspace)) {
                open_urls_in_editor(drop_event->mimeData()->urls());
            } else {
                add_paths(drop_event->mimeData()->urls());
            }
            drop_event->acceptProposedAction();
            return true;
        }
    }

    if (object == m_entry_view && event->type() == QEvent::KeyPress) {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
            activate_current_entry();
            return true;
        }
        if (
            key_event->key() == Qt::Key_Backspace &&
            m_entry_model != nullptr &&
            m_entry_model->flat_mode() &&
            m_entry_model->flat_can_go_up()
        ) {
            set_entry_list_path(QString::fromStdString(m_entry_model->flat_parent_path()));
            return true;
        }
    }

    return QMainWindow::eventFilter(object, event);
}

void MainWindow::open_urls_in_editor(const QList<QUrl>& urls) {
    if (m_editor_workspace == nullptr) {
        return;
    }

    size_t opened = 0;
    for (const auto& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const auto path = path_from_qstring(url.toLocalFile());
        std::string reason;
        auto document = load_document_summary(path, reason, m_decryption_keys);
        if (!document || !supports_editor(*document)) {
            continue;
        }
        EditorOpenRequest request;
        request.source_kind = EditorOpenRequest::SourceKind::Path;
        request.display_name = path.filename().string();
        request.source_path = path;
        request.keys = m_decryption_keys;
        request.detected_format = document->format;
        request.document = std::move(*document);
        m_editor_workspace->open_request(std::move(request));
        ++opened;
    }

    if (opened == 0) {
        statusBar()->showMessage(QStringLiteral("No local files dropped for Editor"), 3000);
        return;
    }
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Opened %1 dropped file(s) in Editor").arg(opened));
    statusBar()->showMessage(QStringLiteral("Opened dropped file(s) in Editor"), 3000);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    update_preview_image();
    schedule_position_edge_buttons();
    if (m_drop_overlay != nullptr) {
        const auto bounds = rect().adjusted(18, menuBar()->height() + 18, -18, -18);
        m_drop_overlay->setGeometry(bounds);
        m_drop_overlay->raise();
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    schedule_position_edge_buttons();
}

void MainWindow::set_drop_overlay_visible(bool visible) {
    if (m_drop_overlay == nullptr) {
        return;
    }
    if (visible) {
        const auto bounds = rect().adjusted(18, menuBar()->height() + 18, -18, -18);
        m_drop_overlay->setGeometry(bounds);
        m_drop_overlay->raise();
        m_drop_overlay->show();
    } else {
        m_drop_overlay->hide();
    }
}













} // namespace cristudio
