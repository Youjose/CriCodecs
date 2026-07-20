#pragma once

#include <QString>

class QTabWidget;
class QWidget;

namespace cristudio {

struct EditorOpenRequest;

[[nodiscard]] QWidget* create_editor_document_widget(EditorOpenRequest request, QTabWidget* tabs);
[[nodiscard]] bool is_editor_document_widget(QWidget* widget);
[[nodiscard]] QString editor_document_tab_title(QWidget* widget);
[[nodiscard]] bool editor_document_is_dirty(QWidget* widget);
[[nodiscard]] bool editor_document_has_background_work(QWidget* widget);
[[nodiscard]] bool editor_document_confirm_close(QWidget* widget, QWidget* parent);

} // namespace cristudio
