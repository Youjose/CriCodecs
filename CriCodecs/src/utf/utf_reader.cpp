/**
 * @file utf_reader.cpp
 * @brief Generic clear @UTF table reader.
 *
 * The generic reader shape follows vgmstream's `cri_utf` model where
 * applicable, then is checked against official CRI UTF Retriever paths in CPK
 * Maker, Medianoche, AtomCraft, and the official binaries. C++23
 * implementation by Youjose.
 */

#include "utf_table.hpp"

#include "../utilities/io_endian.hpp"

#include <memory>
#include <utility>

namespace cricodecs::utf {

using io::read_be;

std::expected<UtfTable, std::string> UtfTable::load(std::span<const uint8_t> data) {
    return load(data, nullptr);
}

std::expected<UtfTable, std::string> UtfTable::load(std::vector<uint8_t>&& data) {
    auto owner = std::make_shared<std::vector<uint8_t>>(std::move(data));
    const std::span<const uint8_t> bytes = *owner;
    return load(bytes, std::move(owner));
}

std::expected<UtfTable, std::string> UtfTable::load(std::span<const uint8_t> data, io::SourceView::Owner owner) {
    if (data.size() < HEADER_SIZE) {
        return std::unexpected("UTF parse failed: data is too small for header");
    }

    UtfTable table;
    table.m_source = io::SourceView(data, std::move(owner));

    auto result = table.parse_header();
    if (!result) return std::unexpected(result.error());

    result = table.parse_schema();
    if (!result) return std::unexpected(result.error());

    return table;
}

std::expected<UtfTable, std::string> UtfTable::load(const std::filesystem::path& path) {
    auto reader = std::make_shared<io::reader>();
    if (auto result = reader->open(path); !result) {
        return std::unexpected("UTF load failed: failed to open " + path.string() + " (" + result.error() + ")");
    }

    const auto data = reader->data();
    return load(data, std::move(reader));
}

std::expected<void, std::string> UtfTable::parse_header() {
    const uint8_t* buf = m_source.data();

    uint32_t magic = read_be<uint32_t>(buf + 0x00);
    if (magic != MAGIC_UTF) {
        return std::unexpected("UTF parse failed: invalid magic");
    }

    m_table_size     = read_be<uint32_t>(buf + 0x04) + 0x08;
    m_version        = read_be<uint16_t>(buf + 0x08);
    m_rows_offset    = read_be<uint16_t>(buf + 0x0A) + 0x08;
    m_strings_offset = read_be<uint32_t>(buf + 0x0C) + 0x08;
    m_data_offset    = read_be<uint32_t>(buf + 0x10) + 0x08;
    m_name_offset    = read_be<uint32_t>(buf + 0x14);
    m_serialized_column_count = read_be<uint16_t>(buf + 0x18);
    m_row_width      = read_be<uint16_t>(buf + 0x1A);
    m_num_rows       = read_be<uint32_t>(buf + 0x1C);

    if (m_version != 0x00 && m_version != 0x01) {
        return std::unexpected("UTF parse failed: unknown version: " + std::to_string(m_version));
    }

    if (m_table_size > m_source.size()) {
        return std::unexpected("UTF parse failed: table size exceeds data size");
    }
    if (m_rows_offset < HEADER_SIZE ||
        m_rows_offset > m_strings_offset ||
        m_strings_offset > m_data_offset ||
        m_data_offset > m_table_size) {
        return std::unexpected("UTF parse failed: invalid section offsets");
    }

    uint32_t schema_offset = HEADER_SIZE;
    uint32_t schema_size = m_rows_offset - schema_offset;
    uint32_t strings_size = m_data_offset - m_strings_offset;

    if (strings_size == 0 || m_name_offset >= strings_size) {
        return std::unexpected("UTF parse failed: invalid string table");
    }
    if (m_serialized_column_count == 0) {
        return std::unexpected("UTF parse failed: table has no columns");
    }

    m_schema_buf.assign(m_source.begin() + schema_offset, m_source.begin() + schema_offset + schema_size);

    m_string_table.assign(
        reinterpret_cast<const char*>(m_source.data() + m_strings_offset),
        strings_size
    );

    m_table_name = string_at(m_name_offset);
    m_columns.reserve(m_serialized_column_count);

    return {};
}

std::expected<void, std::string> UtfTable::parse_schema() {
    const uint8_t* buf = m_schema_buf.data();
    uint32_t pos = 0;
    uint32_t column_offset = 0;

    for (uint16_t i = 0; i < m_serialized_column_count; ++i) {
        if (pos + 5 > m_schema_buf.size()) {
            return std::unexpected("UTF parse failed: schema ended before column " + std::to_string(i));
        }

        uint8_t info = buf[pos];
        uint8_t flag_byte = info & 0xF0;
        uint8_t type_byte = info & 0x0F;

        ColumnFlag flag = static_cast<ColumnFlag>(flag_byte);
        ColumnType type = static_cast<ColumnType>(type_byte);

        if (flag_byte == 0 || !has_flag(flag, ColumnFlag::Name)) {
            return std::unexpected("UTF parse failed: invalid column flag at column " + std::to_string(i));
        }

        uint32_t name_offset = read_be<uint32_t>(buf + pos + 1);
        if (name_offset >= m_string_table.size()) {
            return std::unexpected("UTF parse failed: invalid column name offset");
        }
        pos += 5;

        uint32_t value_size = get_type_size(type);
        if (value_size == 0) {
            return std::unexpected("UTF parse failed: unknown column type: " + std::to_string(type_byte));
        }

        Column col;
        col.name = string_at(name_offset);
        col.type = type;
        col.flag = flag;
        col.default_offset = 0;
        col.row_offset = 0;

        if (has_flag(flag, ColumnFlag::Default)) {
            if (pos + value_size > m_schema_buf.size()) {
                return std::unexpected("UTF parse failed: default value is out of bounds");
            }
            col.default_offset = pos;
            pos += value_size;
        }
        if (has_flag(flag, ColumnFlag::Row)) {
            col.row_offset = column_offset;
            column_offset += value_size;
        }

        m_columns.push_back(std::move(col));
    }

    const uint64_t rows_size = static_cast<uint64_t>(m_num_rows) * m_row_width;
    if (static_cast<uint64_t>(m_rows_offset) + rows_size > m_strings_offset) {
        return std::unexpected("UTF parse failed: row data exceeds row section");
    }

    m_default_values.assign(m_columns.size(), std::monostate{});

    return {};
}

} // namespace cricodecs::utf
