#include "../main_window.hpp"

#include "../modules/acb/acb_cue_view.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QListView>
#include <QToolButton>
#include <QTreeView>

namespace cristudio {

void MainWindow::update_entry_view_mode_labels(bool cue_view) {
    const auto first = cue_view
        ? QCoreApplication::translate("MainWindow.AcbCuePreview", "Cue")
        : QCoreApplication::translate("MainWindow.AcbCuePreview", "Tree");
    const auto list =
        QCoreApplication::translate("MainWindow.AcbCuePreview", "List");
    if (m_entry_view_mode != nullptr && m_entry_view_mode->count() >= 2) {
        m_entry_view_mode->setItemText(0, first);
        m_entry_view_mode->setItemText(1, list);
    }
    if (m_acb_cue_selector_form != nullptr &&
        m_acb_cue_route_combo != nullptr) {
        if (auto* label =
                qobject_cast<QLabel*>(
                    m_acb_cue_selector_form->labelForField(
                        m_acb_cue_route_combo));
            label != nullptr) {
            label->setText(QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Playback path"));
        }
    }
    if (m_entry_view_mode == nullptr ||
        m_entry_view_mode->parentWidget() == nullptr) {
        return;
    }
    for (auto* button :
         m_entry_view_mode->parentWidget()->findChildren<QToolButton*>(
             QStringLiteral("EntryViewModeSegment"))) {
        const auto mode = button->property("modeValue").toInt();
        button->setText(mode == 0 ? first : list);
        button->setToolTip(
            cue_view && mode == 0
                ? QCoreApplication::translate(
                      "MainWindow.AcbCuePreview",
                      "Show authored ACB cues")
                : QCoreApplication::translate(
                      "MainWindow.AcbCuePreview",
                      "Show archive entries as %1")
                      .arg((mode == 0 ? first : list).toLower()));
    }
}

void MainWindow::apply_current_entry_view_mode() {
    if (m_entry_model == nullptr || m_entry_view == nullptr ||
        m_entry_view_mode == nullptr) {
        return;
    }

    const LoadedDocument* document = nullptr;
    if (m_file_view != nullptr && m_file_proxy != nullptr &&
        m_file_model != nullptr && m_file_view->currentIndex().isValid()) {
        document = m_file_model->document_at(
            m_file_proxy->mapToSource(m_file_view->currentIndex()).row());
    }
    if (document == nullptr) {
        return;
    }

    const auto current = m_entry_view->currentIndex();
    if (current.isValid() && m_entry_proxy != nullptr) {
        const auto source = m_entry_proxy->mapToSource(current);
        if (const auto* summary = m_entry_model->summary_at(source);
            summary != nullptr) {
            if (m_showing_acb_cues) {
                m_acb_last_cue_index = summary->source_index;
            } else if (m_acb_cue_sheet != nullptr) {
                m_acb_last_waveform_index = summary->source_index;
            }
        }
    }

    if (m_acb_cue_sheet != nullptr) {
        const auto show_cues = m_entry_view_mode->currentIndex() == 0;
        m_showing_acb_cues = show_cues;
        if (!show_cues) {
            m_pending_acb_cue_preview = false;
        }
        if (show_cues) {
            m_entry_model->set_entries_view(
                m_acb_cue_sheet->entries,
                m_acb_cue_sheet->entry_columns,
                m_acb_cue_sheet->entry_column_types,
                false,
                {});
        } else {
            m_entry_model->set_entries_view(
                document->entries,
                document->entry_columns,
                document->entry_column_types,
                false,
                {});
        }
        m_entry_view_mode->setEnabled(true);
        m_entry_view->setRootIsDecorated(false);
        update_entry_path_bar();
        fit_entry_columns(m_entry_view, true);

        const auto preferred = show_cues
            ? m_acb_last_cue_index
            : m_acb_last_waveform_index;
        if (preferred && m_entry_proxy != nullptr) {
            for (int row = 0; row < m_entry_model->rowCount(); ++row) {
                const auto source = m_entry_model->index(row, 0);
                const auto* summary = m_entry_model->summary_at(source);
                if (summary == nullptr ||
                    summary->source_index != *preferred) {
                    continue;
                }
                const auto proxy = m_entry_proxy->mapFromSource(source);
                if (proxy.isValid()) {
                    m_entry_view->setCurrentIndex(proxy);
                    m_entry_view->scrollTo(proxy);
                }
                break;
            }
        }
        return;
    }

    m_showing_acb_cues = false;
    m_pending_acb_cue_preview = false;
    const auto has_custom_columns = !document->entry_columns.empty();
    const auto flat_mode =
        m_entry_view_mode->currentIndex() == 1 && !has_custom_columns;
    m_entry_model->set_entries_view(
        document->entries,
        document->entry_columns,
        document->entry_column_types,
        flat_mode,
        {});
    m_entry_view_mode->setEnabled(!has_custom_columns);
    m_entry_view->setRootIsDecorated(
        !m_entry_model->flat_mode() &&
        !m_entry_model->has_custom_columns());
    update_entry_path_bar();
    fit_entry_columns(m_entry_view, has_custom_columns);
    if (!flat_mode && !has_custom_columns) {
        m_entry_view->expandToDepth(6);
    }
}

} // namespace cristudio
