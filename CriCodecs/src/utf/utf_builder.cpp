/**
 * @file utf_builder.cpp
 * @brief Generic clear @UTF table builder.
 *
 * UTF layout behavior is validated against official CRI UTF Maker paths in CPK
 * Maker, Medianoche and AtomCraft. Generic builder
 * implementation by Youjose.
 */

#include "utf_table.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstring>
#include <type_traits>
#include <utility>

namespace cricodecs::utf {

using io::write_be;
using util::align_up;

UtfTable UtfTable::create(std::string_view name, uint16_t version) {
    UtfTable table;
    table.m_table_name = std::string(name);
    table.m_version = version;
    table.m_num_rows = 0;
    return table;
}

void UtfTable::add_column(std::string_view name, ColumnType type, ColumnFlag flag) {
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    Column col;
    col.name = std::string(name);
    col.type = type;
    col.flag = flag;
    col.default_offset = 0;
    col.row_offset = 0;
    m_columns.push_back(std::move(col));
    m_default_values.push_back(std::monostate{});
    for (auto& row : m_values) {
        row.resize(m_columns.size(), std::monostate{});
    }
    m_column_cache.clear();
}

uint32_t UtfTable::add_row() {
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    uint32_t row_idx = m_num_rows++;
    m_values.resize(m_num_rows);
    m_values[row_idx].resize(m_columns.size(), std::monostate{});
    return row_idx;
}

void UtfTable::set(uint32_t row, uint32_t col, Value value) {
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    if (row >= m_values.size() || col >= m_columns.size()) return;
    m_values[row][col] = std::move(value);
}

void UtfTable::set_default_value(uint32_t col, Value value) {
    if (m_values.empty() && m_num_rows > 0) {
        *this = editable_copy();
    }
    if (col >= m_columns.size()) return;
    if (col >= m_default_values.size()) {
        m_default_values.resize(m_columns.size(), std::monostate{});
    }
    m_default_values[col] = std::move(value);
    m_columns[col].flag = m_columns[col].flag | ColumnFlag::Default;
}

UtfTable UtfTable::editable_copy() const {
    auto editable = UtfTable::create(m_table_name, m_version);
    editable.set_text_encoding(m_text_encoding);
    if (m_row_width != 0) {
        editable.set_row_width(m_row_width);
    }
    if (m_data_alignment != 0) {
        editable.set_data_alignment(m_data_alignment);
    }

    for (uint32_t column = 0; column < column_count(); ++column) {
        const auto& col = m_columns[column];
        editable.add_column(col.name, col.type, col.flag);
        if (has_flag(col.flag, ColumnFlag::Default)) {
            if (col.type == ColumnType::VLData) {
                if (auto data = get_default_data(column)) {
                    editable.set_default_value(column, std::vector<uint8_t>(data->begin(), data->end()));
                }
            } else if (auto value = get_default_value(column)) {
                editable.set_default_value(column, *value);
            }
        }
    }

    for (uint32_t row = 0; row < row_count(); ++row) {
        editable.add_row();
        for (uint32_t column = 0; column < column_count(); ++column) {
            const auto& col = m_columns[column];
            if (!has_flag(col.flag, ColumnFlag::Row)) {
                continue;
            }
            if (col.type == ColumnType::VLData) {
                if (auto data = get_data(row, column)) {
                    editable.set(row, column, std::vector<uint8_t>(data->begin(), data->end()));
                }
            } else if (auto value = get_value(row, column)) {
                editable.set(row, column, *value);
            }
        }
    }

    return editable;
}

std::vector<uint8_t> UtfTable::build() const {
    std::vector<ColumnFlag> flags(m_columns.size(), ColumnFlag::Name);

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (has_flag(m_columns[c].flag, ColumnFlag::Row) ||
            has_flag(m_columns[c].flag, ColumnFlag::Default)) {
            flags[c] = m_columns[c].flag;
            continue;
        }

        if (m_num_rows == 0) {
            flags[c] = ColumnFlag::Name;
        } else if (m_num_rows == 1) {
            if (std::holds_alternative<std::monostate>(m_values[0][c])) {
                flags[c] = ColumnFlag::Name;
            } else {
                flags[c] = ColumnFlag::Name | ColumnFlag::Row;
            }
        } else {
            bool all_mono = true;
            bool all_same = true;
            for (uint32_t r = 0; r < m_num_rows; ++r) {
                if (!std::holds_alternative<std::monostate>(m_values[r][c]))
                    all_mono = false;
            }
            if (all_mono) {
                flags[c] = ColumnFlag::Name;
                continue;
            }

            for (uint32_t r = 1; r < m_num_rows && all_same; ++r) {
                if (m_values[r][c].index() != m_values[0][c].index()) {
                    all_same = false;
                } else {
                    std::visit([&](auto&& val0) {
                        using T = std::decay_t<decltype(val0)>;
                        if constexpr (std::same_as<T, std::monostate>) {
                            all_same = true;
                        } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
                            auto& val_r = std::get<std::vector<uint8_t>>(m_values[r][c]);
                            all_same = (val0 == val_r);
                        } else if constexpr (std::same_as<T, GUID>) {
                            auto& val_r = std::get<GUID>(m_values[r][c]);
                            all_same = (val0 == val_r);
                        } else if constexpr (std::same_as<T, DataRef>) {
                            auto& val_r = std::get<DataRef>(m_values[r][c]);
                            all_same = (val0.offset == val_r.offset && val0.size == val_r.size);
                        } else {
                            all_same = (val0 == std::get<T>(m_values[r][c]));
                        }
                    }, m_values[0][c]);
                }
            }

            if (all_same && !std::holds_alternative<std::monostate>(m_values[0][c])) {
                flags[c] = ColumnFlag::Name | ColumnFlag::Default;
            } else {
                flags[c] = ColumnFlag::Name | ColumnFlag::Row;
            }
        }
    }

    auto default_value_for_column = [&](size_t column_index) -> const Value* {
        if (column_index < m_default_values.size() &&
            !std::holds_alternative<std::monostate>(m_default_values[column_index])) {
            return &m_default_values[column_index];
        }
        if (!m_values.empty() && column_index < m_values[0].size() &&
            !std::holds_alternative<std::monostate>(m_values[0][column_index])) {
            return &m_values[0][column_index];
        }
        return nullptr;
    };

    std::vector<std::string> strings;
    cricodecs::util::flat_unordered_map<std::string, uint32_t, cricodecs::util::transparent_string_hash, std::equal_to<>> string_offsets;
    strings.reserve(m_columns.size() + 1);
    string_offsets.reserve(m_columns.size() + 1);
    uint32_t next_string_offset = 0;

    auto add_string = [&](const std::string& s) -> uint32_t {
        auto it = string_offsets.find(s);
        if (it != string_offsets.end()) return it->second;

        const uint32_t offset = next_string_offset;
        next_string_offset += static_cast<uint32_t>(s.size() + 1);
        string_offsets[s] = offset;
        strings.push_back(s);
        return offset;
    };

    if (m_version == 0) {
        add_string("<NULL>");
    }

    uint32_t table_name_offset = add_string(m_table_name);

    std::vector<uint32_t> column_name_offsets;
    column_name_offsets.reserve(m_columns.size());
    for (const auto& col : m_columns) {
        column_name_offsets.push_back(add_string(col.name));
    }

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::String || !has_flag(flags[c], ColumnFlag::Default)) {
            continue;
        }

        const Value* default_value = default_value_for_column(c);
        if (default_value != nullptr && std::holds_alternative<std::string>(*default_value)) {
            add_string(std::get<std::string>(*default_value));
        }
    }

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::String) continue;
        for (uint32_t r = 0; r < m_num_rows; ++r) {
            if (std::holds_alternative<std::string>(m_values[r][c])) {
                add_string(std::get<std::string>(m_values[r][c]));
            }
        }
    }

    uint32_t schema_size = 0;
    uint32_t computed_row_width = 0;
    for (size_t c = 0; c < m_columns.size(); ++c) {
        schema_size += 5;
        uint32_t type_size = get_type_size(m_columns[c].type);
        if (has_flag(flags[c], ColumnFlag::Default)) {
            schema_size += type_size;
        }
        if (has_flag(flags[c], ColumnFlag::Row)) {
            computed_row_width += type_size;
        }
    }

    uint32_t header_row_width = (m_row_width > 0) ? m_row_width : computed_row_width;

    uint32_t rows_offset = HEADER_SIZE + schema_size;
    uint32_t rows_size = m_num_rows * computed_row_width;

    std::vector<uint8_t> string_blob;
    for (const auto& s : strings) {
        string_blob.insert(string_blob.end(), s.begin(), s.end());
        string_blob.push_back(0);
    }

    uint32_t strings_offset = rows_offset + rows_size;
    uint32_t strings_size = static_cast<uint32_t>(string_blob.size());

    std::vector<uint8_t> data_blob;
    std::unordered_map<const void*, uint32_t> data_offsets;

    auto append_vldata = [&](const void* key, const std::vector<uint8_t>& data) {
        if (data_offsets.contains(key)) {
            return;
        }
        if (m_data_alignment > 0 && !data_blob.empty()) {
            // ACB-style UTF payloads expect a gap before each aligned VLData entry.
            const uint32_t aligned = align_up(static_cast<uint32_t>(data_blob.size()) + 1, m_data_alignment);
            data_blob.resize(aligned, 0);
        }
        data_offsets[key] = static_cast<uint32_t>(data_blob.size());
        data_blob.insert(data_blob.end(), data.begin(), data.end());
    };

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::VLData || !has_flag(flags[c], ColumnFlag::Default)) {
            continue;
        }
        const Value* default_value = default_value_for_column(c);
        if (default_value != nullptr && std::holds_alternative<std::vector<uint8_t>>(*default_value)) {
            const auto& data = std::get<std::vector<uint8_t>>(*default_value);
            if (!data.empty()) {
                append_vldata(default_value, data);
            } else {
                data_offsets[default_value] = 0;
            }
        }
    }

    for (uint32_t r = 0; r < m_num_rows; ++r) {
        for (size_t c = 0; c < m_columns.size(); ++c) {
            if (m_columns[c].type == ColumnType::VLData) {
                if (std::holds_alternative<std::vector<uint8_t>>(m_values[r][c])) {
                    const auto& data = std::get<std::vector<uint8_t>>(m_values[r][c]);
                    if (!data.empty()) {
                        append_vldata(&m_values[r][c], data);
                    } else {
                        data_offsets[&m_values[r][c]] = 0;
                    }
                }
            }
        }
    }

    bool has_data = !data_blob.empty();
    uint32_t data_offset = strings_offset + strings_size;

    if (m_data_alignment > 0 && has_data) {
        data_offset = align_up(data_offset, m_data_alignment);
    }

    uint32_t total_size = data_offset + static_cast<uint32_t>(data_blob.size());
    uint32_t final_align = (m_data_alignment > 0) ? m_data_alignment : 8;
    uint32_t padded_size = align_up(total_size, final_align);

    if (!has_data) {
        data_offset = padded_size;
    }

    std::vector<uint8_t> output(padded_size, 0);
    uint8_t* buf = output.data();

    write_be<uint32_t>(buf + 0x00, MAGIC_UTF);
    write_be<uint32_t>(buf + 0x04, padded_size - 0x08);
    write_be<uint16_t>(buf + 0x08, m_version);
    write_be<uint16_t>(buf + 0x0A, static_cast<uint16_t>(rows_offset - 0x08));
    write_be<uint32_t>(buf + 0x0C, strings_offset - 0x08);
    write_be<uint32_t>(buf + 0x10, data_offset - 0x08);
    write_be<uint32_t>(buf + 0x14, table_name_offset);
    write_be<uint16_t>(buf + 0x18, static_cast<uint16_t>(m_columns.size()));
    write_be<uint16_t>(buf + 0x1A, static_cast<uint16_t>(header_row_width));
    write_be<uint32_t>(buf + 0x1C, m_num_rows);

    auto write_value = [&](uint8_t* dst, const Value& val, [[maybe_unused]] ColumnType type) {
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::monostate>) {
            } else if constexpr (std::same_as<T, uint8_t> || std::same_as<T, int8_t>) {
                dst[0] = static_cast<uint8_t>(v);
            } else if constexpr (std::same_as<T, uint16_t>) {
                write_be<uint16_t>(dst, v);
            } else if constexpr (std::same_as<T, int16_t>) {
                write_be<int16_t>(dst, v);
            } else if constexpr (std::same_as<T, uint32_t>) {
                write_be<uint32_t>(dst, v);
            } else if constexpr (std::same_as<T, int32_t>) {
                write_be<int32_t>(dst, v);
            } else if constexpr (std::same_as<T, uint64_t>) {
                write_be<uint64_t>(dst, v);
            } else if constexpr (std::same_as<T, int64_t>) {
                write_be<int64_t>(dst, v);
            } else if constexpr (std::same_as<T, float>) {
                write_be<uint32_t>(dst, std::bit_cast<uint32_t>(v));
            } else if constexpr (std::same_as<T, double>) {
                write_be<uint64_t>(dst, std::bit_cast<uint64_t>(v));
            } else if constexpr (std::same_as<T, std::string>) {
                write_be<uint32_t>(dst, string_offsets.at(v));
            } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
                auto it = data_offsets.find(&val);
                uint32_t off = (it != data_offsets.end()) ? it->second : 0;
                write_be<uint32_t>(dst, off);
                write_be<uint32_t>(dst + 4, static_cast<uint32_t>(v.size()));
            } else if constexpr (std::same_as<T, GUID>) {
                std::memcpy(dst, v.data, 16);
            } else if constexpr (std::same_as<T, DataRef>) {
                write_be<uint32_t>(dst, v.offset);
                write_be<uint32_t>(dst + 4, v.size);
            }
        }, val);
    };

    uint32_t schema_pos = HEADER_SIZE;
    for (size_t c = 0; c < m_columns.size(); ++c) {
        uint8_t info = static_cast<uint8_t>(flags[c]) | static_cast<uint8_t>(m_columns[c].type);
        buf[schema_pos++] = info;
        write_be<uint32_t>(buf + schema_pos, column_name_offsets[c]);
        schema_pos += 4;

        if (has_flag(flags[c], ColumnFlag::Default)) {
            if (const Value* default_value = default_value_for_column(c); default_value != nullptr) {
                write_value(buf + schema_pos, *default_value, m_columns[c].type);
            }
            schema_pos += get_type_size(m_columns[c].type);
        }
    }

    uint32_t row_pos = rows_offset;
    for (uint32_t r = 0; r < m_num_rows; ++r) {
        uint32_t col_pos = row_pos;
        for (size_t c = 0; c < m_columns.size(); ++c) {
            if (!has_flag(flags[c], ColumnFlag::Row)) continue;
            write_value(buf + col_pos, m_values[r][c], m_columns[c].type);
            col_pos += get_type_size(m_columns[c].type);
        }
        row_pos += computed_row_width;
    }

    std::memcpy(buf + strings_offset, string_blob.data(), string_blob.size());

    if (!data_blob.empty() && data_offset + data_blob.size() <= output.size()) {
        std::memcpy(buf + data_offset, data_blob.data(), data_blob.size());
    }

    return output;
}

} // namespace cricodecs::utf
