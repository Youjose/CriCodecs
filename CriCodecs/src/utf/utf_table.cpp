/**
 * @file utf_table.cpp
 * @brief Shared @UTF table object helpers.
 *
 * The query model follows vgmstream's `cri_utf` reader shape where applicable,
 * while the table object also supports the current CriCodecs builder/mutation
 * surface. Validation against official CRI binaries and samples by Youjose.
 */

#include "utf_table.hpp"

#include "../utilities/io_endian.hpp"

#include <bit>
#include <cstring>
#include <utility>

namespace cricodecs::utf {

using io::read_be;

std::string_view UtfTable::string_at(uint32_t offset) const {
    if (offset >= m_string_table.size()) {
        return "";
    }
    const char* begin = m_string_table.data() + offset;
    const size_t remaining = m_string_table.size() - offset;
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', remaining));
    const size_t len = end == nullptr ? remaining : static_cast<size_t>(end - begin);
    return std::string_view(m_string_table.data() + offset, len);
}

int UtfTable::find_column(std::string_view name) const {
    std::string key(name);
    auto it = m_column_cache.find(key);
    if (it != m_column_cache.end()) {
        return it->second;
    }

    for (size_t i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i].name == name) {
            m_column_cache[key] = static_cast<int>(i);
            return static_cast<int>(i);
        }
    }
    m_column_cache[key] = -1;
    return -1;
}

void UtfTable::set_table_name(std::string_view name) {
    m_table_name = std::string(name);
}

bool UtfTable::rename_column(uint32_t col, std::string_view name) {
    if (col >= m_columns.size()) {
        return false;
    }
    m_columns[col].name = std::string(name);
    m_column_cache.clear();
    return true;
}

bool UtfTable::set_column_type(uint32_t col, ColumnType type) {
    if (col >= m_columns.size() || get_type_size(type) == 0) {
        return false;
    }
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    if (m_columns[col].type == type) {
        return true;
    }
    m_columns[col].type = type;
    if (col < m_default_values.size()) {
        m_default_values[col] = std::monostate{};
    }
    for (auto& row : m_values) {
        if (col < row.size()) {
            row[col] = std::monostate{};
        }
    }
    return true;
}

bool UtfTable::set_column_flag(uint32_t col, ColumnFlag flag) {
    if (col >= m_columns.size() || !has_flag(flag, ColumnFlag::Name)) {
        return false;
    }
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    m_columns[col].flag = flag;
    return true;
}

bool UtfTable::remove_row(uint32_t row) {
    if (row >= m_num_rows) {
        return false;
    }
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    if (row < m_values.size()) {
        m_values.erase(m_values.begin() + row);
    }
    --m_num_rows;
    return true;
}

bool UtfTable::remove_column(uint32_t col) {
    if (col >= m_columns.size()) {
        return false;
    }
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    m_columns.erase(m_columns.begin() + col);
    if (col < m_default_values.size()) {
        m_default_values.erase(m_default_values.begin() + col);
    }
    for (auto& row : m_values) {
        if (col < row.size()) {
            row.erase(row.begin() + col);
        }
    }
    m_column_cache.clear();
    return true;
}

Value UtfTable::read_value_at(const uint8_t* buf, ColumnType type) const {
    switch (type) {
        case ColumnType::UInt8:  return static_cast<uint8_t>(buf[0]);
        case ColumnType::SInt8:  return static_cast<int8_t>(buf[0]);
        case ColumnType::UInt16: return read_be<uint16_t>(buf);
        case ColumnType::SInt16: return read_be<int16_t>(buf);
        case ColumnType::UInt32: return read_be<uint32_t>(buf);
        case ColumnType::SInt32: return read_be<int32_t>(buf);
        case ColumnType::UInt64: return read_be<uint64_t>(buf);
        case ColumnType::SInt64: return read_be<int64_t>(buf);
        case ColumnType::Float: {
            uint32_t bits = read_be<uint32_t>(buf);
            return std::bit_cast<float>(bits);
        }
        case ColumnType::Double: {
            uint64_t bits = read_be<uint64_t>(buf);
            return std::bit_cast<double>(bits);
        }
        case ColumnType::String: {
            uint32_t str_offset = read_be<uint32_t>(buf);
            return std::string(string_at(str_offset));
        }
        case ColumnType::VLData: {
            DataRef ref;
            ref.offset = read_be<uint32_t>(buf + 0);
            ref.size = read_be<uint32_t>(buf + 4);
            return ref;
        }
        case ColumnType::GUID: {
            GUID guid;
            std::memcpy(guid.data, buf, 16);
            return guid;
        }
        default:
            return std::monostate{};
    }
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::field_data(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= m_num_rows) {
        return std::unexpected("UTF row index is out of range");
    }

    const Column& column = m_columns[col];
    const uint32_t field_size = get_type_size(column.type);

    if (has_flag(column.flag, ColumnFlag::Row)) {
        const uint32_t row_offset = m_rows_offset + row * m_row_width + column.row_offset;
        if (row_offset + field_size > m_source.size()) {
            return std::unexpected("UTF row data is out of bounds");
        }
        return std::span<const uint8_t>(m_source.data() + row_offset, field_size);
    }

    if (has_flag(column.flag, ColumnFlag::Default)) {
        if (m_schema_buf.empty()) {
            return std::unexpected("UTF schema data is unavailable");
        }
        if (column.default_offset + field_size > m_schema_buf.size()) {
            return std::unexpected("UTF default value is out of bounds");
        }
        return std::span<const uint8_t>(m_schema_buf.data() + column.default_offset, field_size);
    }

    return std::unexpected("UTF column has no data");
}

std::expected<Value, std::string> UtfTable::get_value(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= m_num_rows) {
        return std::unexpected("UTF row index is out of range");
    }
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        !std::holds_alternative<std::monostate>(m_values[row][col])
    ) {
        return m_values[row][col];
    }

    auto field = field_data(row, col);
    if (!field) {
        if (field.error() == "UTF column has no data") {
            return Value{std::monostate{}};
        }
        return std::unexpected(field.error());
    }

    return read_value_at(field->data(), m_columns[col].type);
}

std::expected<Value, std::string> UtfTable::get_default_value(uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }

    const Column& column = m_columns[col];
    if (!has_flag(column.flag, ColumnFlag::Default)) {
        return std::unexpected("UTF column has no default value");
    }

    if (col < m_default_values.size() && !std::holds_alternative<std::monostate>(m_default_values[col])) {
        return m_default_values[col];
    }

    if (!m_schema_buf.empty()) {
        return read_value_at(m_schema_buf.data() + column.default_offset, column.type);
    }

    return std::monostate{};
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::get_data(uint32_t row, uint32_t col) const {
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        std::holds_alternative<std::vector<uint8_t>>(m_values[row][col])
    ) {
        const auto& bytes = std::get<std::vector<uint8_t>>(m_values[row][col]);
        return std::span<const uint8_t>(bytes.data(), bytes.size());
    }

    auto val = get_value(row, col);
    if (!val) return std::unexpected(val.error());

    if (!std::holds_alternative<DataRef>(*val)) {
        return std::unexpected("UTF column is not VLData type");
    }

    DataRef ref = std::get<DataRef>(*val);
    uint32_t abs_offset = m_data_offset + ref.offset;
    if (abs_offset + ref.size > m_source.size()) {
        return std::unexpected("UTF data reference is out of bounds");
    }

    return std::span<const uint8_t>(m_source.data() + abs_offset, ref.size);
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::get_default_data(uint32_t col) const {
    auto val = get_default_value(col);
    if (!val) return std::unexpected(val.error());

    if (std::holds_alternative<std::vector<uint8_t>>(*val)) {
        const auto& bytes = std::get<std::vector<uint8_t>>(*val);
        return std::span<const uint8_t>(bytes.data(), bytes.size());
    }

    if (!std::holds_alternative<DataRef>(*val)) {
        return std::unexpected("UTF column is not VLData type");
    }

    DataRef ref = std::get<DataRef>(*val);
    uint32_t abs_offset = m_data_offset + ref.offset;
    if (abs_offset + ref.size > m_source.size()) {
        return std::unexpected("UTF data reference is out of bounds");
    }

    return std::span<const uint8_t>(m_source.data() + abs_offset, ref.size);
}

std::expected<std::string_view, std::string> UtfTable::get_string(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= m_num_rows) {
        return std::unexpected("UTF row index is out of range");
    }
    if (m_columns[col].type != ColumnType::String) {
        return std::unexpected("UTF column is not String type");
    }
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        std::holds_alternative<std::string>(m_values[row][col])
    ) {
        return std::string_view(std::get<std::string>(m_values[row][col]));
    }

    auto field = field_data(row, col);
    if (!field) {
        return std::unexpected(field.error());
    }

    const uint32_t str_offset = read_be<uint32_t>(field->data());
    return string_at(str_offset);
}

#define DEFINE_GET_SPECIALIZATION(cpp_type, column_type, read_expression) \
    template<> \
    std::expected<cpp_type, std::string> UtfTable::get<cpp_type>(uint32_t row, uint32_t col) const { \
        if (col >= m_columns.size()) { \
            return std::unexpected("UTF column index is out of range"); \
        } \
        const Column& column = m_columns[col]; \
        if (row >= m_num_rows) { \
            return std::unexpected("UTF row index is out of range"); \
        } \
        if (column.type != column_type) { \
            return std::unexpected("UTF value read failed: type mismatch"); \
        } \
        if (row < m_values.size() && col < m_values[row].size() && std::holds_alternative<cpp_type>(m_values[row][col])) { \
            return std::get<cpp_type>(m_values[row][col]); \
        } \
        const auto field = field_data(row, col); \
        if (has_flag(column.flag, ColumnFlag::Default) && !field) { \
            if (col < m_default_values.size() && std::holds_alternative<cpp_type>(m_default_values[col])) { \
                return std::get<cpp_type>(m_default_values[col]); \
            } \
            if (field.error() == "UTF column has no data") { \
                return std::unexpected("UTF value read failed: type mismatch"); \
            } \
            if (field.error() == "UTF schema data is unavailable") { \
                return std::unexpected("UTF value read failed: type mismatch"); \
            } \
            if (field.error() == "UTF default value is out of bounds") { \
                return std::unexpected("UTF value read failed: type mismatch"); \
            } \
            return std::unexpected(field.error()); \
        } \
        if (!field) { \
            return std::unexpected(field.error()); \
        } \
        const uint8_t* buf = field->data(); \
        return read_expression; \
    }

DEFINE_GET_SPECIALIZATION(uint8_t, ColumnType::UInt8, static_cast<uint8_t>(buf[0]))
DEFINE_GET_SPECIALIZATION(int8_t, ColumnType::SInt8, static_cast<int8_t>(buf[0]))
DEFINE_GET_SPECIALIZATION(uint16_t, ColumnType::UInt16, read_be<uint16_t>(buf))
DEFINE_GET_SPECIALIZATION(int16_t, ColumnType::SInt16, read_be<int16_t>(buf))
DEFINE_GET_SPECIALIZATION(uint32_t, ColumnType::UInt32, read_be<uint32_t>(buf))
DEFINE_GET_SPECIALIZATION(int32_t, ColumnType::SInt32, read_be<int32_t>(buf))
DEFINE_GET_SPECIALIZATION(uint64_t, ColumnType::UInt64, read_be<uint64_t>(buf))
DEFINE_GET_SPECIALIZATION(int64_t, ColumnType::SInt64, read_be<int64_t>(buf))
DEFINE_GET_SPECIALIZATION(float, ColumnType::Float, std::bit_cast<float>(read_be<uint32_t>(buf)))
DEFINE_GET_SPECIALIZATION(double, ColumnType::Double, std::bit_cast<double>(read_be<uint64_t>(buf)))

#undef DEFINE_GET_SPECIALIZATION

} // namespace cricodecs::utf
