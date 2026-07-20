#pragma once

#include <QString>

class QTableWidget;

namespace cristudio {

void set_table_item(QTableWidget* table, int row, int column, const QString& text, bool editable);

} // namespace cristudio
