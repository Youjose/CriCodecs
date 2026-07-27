#include "shared/i18n.hpp"
#include "modules/utf/utf_edit.hpp"

#include "modules/utf/utf_common.hpp"

#include <limits>
#include <utility>

namespace cristudio::modules::utf {

ScratchTableSession create_scratch_table_session() {
    auto table = cricodecs::utf::UtfTable::create("NewTable");
    table.add_column(
        cristudio::i18n::translate_utf8("Utf.UtfEdit", "Name"),
        cricodecs::utf::ColumnType::String,
        cricodecs::utf::ColumnFlag::Name | cricodecs::utf::ColumnFlag::Default | cricodecs::utf::ColumnFlag::Row
    );
    table.add_column(
        cristudio::i18n::translate_utf8("Utf.UtfEdit", "Data"),
        cricodecs::utf::ColumnType::VLData,
        cricodecs::utf::ColumnFlag::Name | cricodecs::utf::ColumnFlag::Default | cricodecs::utf::ColumnFlag::Row
    );
    table.set_default_value(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Name"), std::string(cristudio::i18n::translate_utf8("Utf.UtfEdit", "entry"))).value();
    table.set_default_value(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Data"), std::vector<uint8_t>{}).value();
    const auto row = table.add_row();
    table.set(row, cristudio::i18n::translate_utf8("Utf.UtfEdit", "Name"), std::string(cristudio::i18n::translate_utf8("Utf.UtfEdit", "entry"))).value();
    table.set(row, cristudio::i18n::translate_utf8("Utf.UtfEdit", "Data"), std::vector<uint8_t>{}).value();

    auto bytes = build_session_bytes(table);
    return ScratchTableSession{
        .table = std::move(table),
        .bytes = bytes,
        .document = LoadedDocument{
            .display_name = "NewTable.utf",
            .format = cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF table (scratch)"),
            .file_size = bytes.size(),
            .info = {
                {cristudio::i18n::translate_utf8("Utf.UtfEdit", "Source"), cristudio::i18n::translate_utf8("Utf.UtfEdit", "Scratch UTF table")},
                {cristudio::i18n::translate_utf8("Utf.UtfEdit", "Rows"), "1"},
                {cristudio::i18n::translate_utf8("Utf.UtfEdit", "Columns"), "2"}
            },
            .entry_columns = {"#", cristudio::i18n::translate_utf8("Utf.UtfEdit", "Name"), cristudio::i18n::translate_utf8("Utf.UtfEdit", "Data")},
            .entry_column_types = {cristudio::i18n::translate_utf8("Utf.UtfEdit", "row"), cristudio::i18n::translate_utf8("Utf.UtfEdit", "string"), cristudio::i18n::translate_utf8("Utf.UtfEdit", "binary")},
            .entries = {EntrySummary{
                .name = cristudio::i18n::translate_utf8("Utf.UtfEdit", "Row 0"),
                .type = cristudio::i18n::translate_utf8("Utf.UtfEdit", "row"),
                .cells = {"0", cristudio::i18n::translate_utf8("Utf.UtfEdit", "entry"), cristudio::i18n::translate_utf8("Utf.UtfEdit", "0 bytes")},
                .cell_source_indices = {
                    std::numeric_limits<uint32_t>::max(),
                    std::numeric_limits<uint32_t>::max(),
                    1
                },
                .source_format = "UTF",
                .source_index = 1,
                .has_source = true
            }}
        }
    };
}

std::expected<void, std::string> rename_table(cricodecs::utf::UtfTable& utf, std::string name) {
    if (name.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF table name cannot be empty"));
    }
    utf.set_table_name(name);
    return {};
}

std::expected<void, std::string> set_value(
    cricodecs::utf::UtfTable& utf,
    uint32_t row,
    uint32_t col,
    cricodecs::utf::Value value
) {
    if (row >= utf.row_count() || col >= utf.column_count()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF cell index is out of range"));
    }
    return utf.set(row, col, std::move(value));
}

std::expected<void, std::string> rename_column(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    std::string name
) {
    if (name.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Column name cannot be empty"));
    }
    if (!utf.rename_column(col, name)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF column index is out of range"));
    }
    return {};
}

std::expected<void, std::string> set_column_type(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::ColumnType type
) {
    if (!utf.set_column_type(col, type)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Unsupported UTF column type or column index"));
    }
    return {};
}

std::expected<void, std::string> set_column_flag(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::ColumnFlag flag
) {
    if (!utf.set_column_flag(col, flag)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Flags must include name and target an existing column"));
    }
    return {};
}

std::expected<void, std::string> set_default_value(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::Value value
) {
    if (col >= utf.column_count()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF column index is out of range"));
    }
    if (!cricodecs::utf::has_flag(utf.column(col).flag, cricodecs::utf::ColumnFlag::Default)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "Add the default flag before editing a default value"));
    }
    return utf.set_default_value(col, std::move(value));
}

uint32_t add_row(cricodecs::utf::UtfTable& utf) {
    return utf.add_row();
}

std::expected<void, std::string> remove_row(cricodecs::utf::UtfTable& utf, uint32_t row) {
    if (!utf.remove_row(row)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF row index is out of range"));
    }
    return {};
}

void add_column(
    cricodecs::utf::UtfTable& utf,
    std::string name,
    cricodecs::utf::ColumnType type
) {
    utf.add_column(name, type, cricodecs::utf::ColumnFlag::Name | cricodecs::utf::ColumnFlag::Row);
}

std::expected<void, std::string> remove_column(cricodecs::utf::UtfTable& utf, uint32_t col) {
    if (!utf.remove_column(col)) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfEdit", "UTF column index is out of range"));
    }
    return {};
}

std::expected<std::vector<uint8_t>, std::string> cell_bytes(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
) {
    return extract_cell_data(utf, source_index);
}

std::vector<uint8_t> build_session_bytes(const cricodecs::utf::UtfTable& utf) {
    return utf.build();
}

} // namespace cristudio::modules::utf
