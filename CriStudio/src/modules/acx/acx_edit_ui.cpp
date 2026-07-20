#include "modules/acx/acx_edit_ui.hpp"

#include "editor/table_item_helpers.hpp"
#include "path_text.hpp"

#include <QTableWidget>

namespace cristudio::modules::acx {
namespace {

QString entry_type_name(cricodecs::acx::AcxEntryType type) {
    switch (type) {
    case cricodecs::acx::AcxEntryType::adx: return QStringLiteral("adx");
    case cricodecs::acx::AcxEntryType::ogg: return QStringLiteral("ogg");
    case cricodecs::acx::AcxEntryType::unknown: break;
    }
    return QStringLiteral("unknown");
}

} // namespace

void populate_editor_archive_table(QTableWidget* table, const cricodecs::acx::AcxContainer& acx) {
    table->clear();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Index"),
        QStringLiteral("Type"),
        QStringLiteral("Table Row"),
        QStringLiteral("Offset"),
        QStringLiteral("Size"),
        QStringLiteral("Suggested Path")
    });
    table->setRowCount(static_cast<int>(acx.entry_count()));
    for (const auto& entry : acx.entries()) {
        const auto row = static_cast<int>(entry.index);
        set_table_item(table, row, 0, QString::number(entry.index), false);
        set_table_item(table, row, 1, entry_type_name(entry.type), false);
        set_table_item(table, row, 2, QStringLiteral("0x%1").arg(0x08u + entry.index * 0x08u, 0, 16).toUpper(), false);
        set_table_item(table, row, 3, QStringLiteral("0x%1").arg(entry.offset, 0, 16).toUpper(), false);
        set_table_item(table, row, 4, QString::number(entry.size), false);
        set_table_item(table, row, 5, path_to_qstring(entry.suggested_path()), false);
    }
}

} // namespace cristudio::modules::acx
