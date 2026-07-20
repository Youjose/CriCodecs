#pragma once

#include "utf_table.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

class QLineEdit;
class QTableWidget;

namespace cristudio::modules::utf {

struct UtfTableWidgets {
    QLineEdit* table_name = nullptr;
    QTableWidget* grid = nullptr;
    QTableWidget* schema = nullptr;
    bool transpose_single_row = false;
};

struct UtfEditResult {
    bool handled = false;
    bool changed = false;
    bool refresh = false;
    bool show_cell = false;
    int row = -1;
    int column = -1;
    QString warning_title;
    QString error;
    QString change_message;
    QString title;
};

[[nodiscard]] QString utf_type_name(cricodecs::utf::ColumnType type);
[[nodiscard]] QString utf_flag_name(cricodecs::utf::ColumnFlag flag);
[[nodiscard]] std::optional<cricodecs::utf::ColumnType> utf_type_from_name(QString text);
[[nodiscard]] std::optional<cricodecs::utf::ColumnFlag> utf_flag_from_name(QString text);
[[nodiscard]] cricodecs::utf::Value utf_value_for_cell(
    const cricodecs::utf::UtfTable& utf,
    uint32_t row,
    uint32_t col
);
[[nodiscard]] cricodecs::utf::Value utf_default_for_column(
    const cricodecs::utf::UtfTable& utf,
    uint32_t col
);
[[nodiscard]] QString utf_value_text(const cricodecs::utf::Value& value);
[[nodiscard]] QString utf_editable_value_text(const cricodecs::utf::Value& value);
void populate_utf_tables(const cricodecs::utf::UtfTable& utf, UtfTableWidgets widgets);
[[nodiscard]] QString utf_table_preview(
    const cricodecs::utf::UtfTable& table,
    size_t max_rows = 64,
    int max_value_chars = 96
);
[[nodiscard]] std::expected<std::vector<uint8_t>, QString> parse_hex_bytes(
    QString text,
    size_t fixed_size = 0
);
[[nodiscard]] std::expected<cricodecs::utf::Value, QString> parse_utf_value(
    cricodecs::utf::ColumnType type,
    const QString& text
);
[[nodiscard]] UtfEditResult rename_table(cricodecs::utf::UtfTable& utf, QString name);
[[nodiscard]] UtfEditResult set_cell_value(cricodecs::utf::UtfTable& utf, int row, int column, QString text);
[[nodiscard]] UtfEditResult edit_grid_item(cricodecs::utf::UtfTable& utf, int row, int column, QString text);
[[nodiscard]] UtfEditResult edit_schema_item(cricodecs::utf::UtfTable& utf, int row, int column, QString text);
[[nodiscard]] UtfEditResult add_row_action(cricodecs::utf::UtfTable& utf);
[[nodiscard]] UtfEditResult remove_row(cricodecs::utf::UtfTable& utf, int row);
[[nodiscard]] UtfEditResult add_column(cricodecs::utf::UtfTable& utf, QString name, QString type_text);
[[nodiscard]] UtfEditResult remove_column(cricodecs::utf::UtfTable& utf, int column);
[[nodiscard]] UtfEditResult rename_column(cricodecs::utf::UtfTable& utf, int column, QString name);
[[nodiscard]] UtfEditResult replace_binary_cell(
    cricodecs::utf::UtfTable& utf,
    int row,
    int column,
    std::vector<uint8_t> bytes
);

} // namespace cristudio::modules::utf
