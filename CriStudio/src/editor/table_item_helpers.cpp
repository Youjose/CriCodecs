#include "editor/table_item_helpers.hpp"

#include <QTableWidget>
#include <QTableWidgetItem>

namespace cristudio {

void set_table_item(QTableWidget* table, int row, int column, const QString& text, bool editable) {
    auto* item = new QTableWidgetItem(text);
    auto flags = item->flags();
    if (editable) {
        flags |= Qt::ItemIsEditable;
    } else {
        flags &= ~Qt::ItemIsEditable;
    }
    item->setFlags(flags);
    table->setItem(row, column, item);
}

} // namespace cristudio
