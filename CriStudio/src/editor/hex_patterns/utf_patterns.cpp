#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cristudio::hexpatterns {

[[nodiscard]] bool is_utf_magic(std::span<const uint8_t> bytes, size_t offset) {
    return has(bytes, offset, "@UTF") || has(bytes, offset, "EUTF");
}

struct UtfPatternColumn {
    uint8_t type = 0;
    uint8_t flag = 0;
    uint32_t row_offset = 0;
    std::string name;
};

[[nodiscard]] uint32_t utf_type_size(uint8_t type) {
    switch (type) {
        case 0x00:
        case 0x01:
            return 1;
        case 0x02:
        case 0x03:
            return 2;
        case 0x04:
        case 0x05:
        case 0x08:
        case 0x0A:
            return 4;
        case 0x06:
        case 0x07:
        case 0x09:
        case 0x0B:
            return 8;
        case 0x0C:
            return 16;
        default:
            return 0;
    }
}

[[nodiscard]] std::string utf_string_at(
    std::span<const uint8_t> prefix,
    size_t base,
    uint64_t strings_offset,
    uint64_t data_offset,
    uint32_t offset
) {
    const auto begin = base + static_cast<size_t>(strings_offset) + offset;
    const auto end = base + static_cast<size_t>(data_offset);
    if (begin >= prefix.size() || begin >= end) {
        return {};
    }
    const auto limit = std::min(end, prefix.size());
    size_t cursor = begin;
    while (cursor < limit && prefix[cursor] != 0) {
        ++cursor;
    }
    return std::string(
        reinterpret_cast<const char*>(prefix.data() + begin),
        cursor - begin
    );
}

[[nodiscard]] std::vector<UtfPatternColumn> utf_pattern_columns(
    std::span<const uint8_t> prefix,
    size_t base,
    uint16_t num_columns,
    uint64_t rows_offset,
    uint64_t strings_offset,
    uint64_t data_offset
) {
    std::vector<UtfPatternColumn> columns;
    columns.reserve(num_columns);

    size_t pos = base + 0x20;
    const auto schema_end = base + static_cast<size_t>(rows_offset);
    uint32_t row_offset = 0;
    for (uint16_t index = 0; index < num_columns; ++index) {
        if (pos + 5 > prefix.size() || pos + 5 > schema_end) {
            break;
        }
        const uint8_t info = prefix[pos];
        const uint8_t flag = info & 0xF0u;
        const uint8_t type = info & 0x0Fu;
        const uint32_t name_offset = cricodecs::io::read_be<uint32_t>(prefix.data() + pos + 1);
        pos += 5;

        const uint32_t size = utf_type_size(type);
        if (size == 0) {
            break;
        }

        UtfPatternColumn column{
            .type = type,
            .flag = flag,
            .row_offset = row_offset,
            .name = utf_string_at(prefix, base, strings_offset, data_offset, name_offset)
        };

        if ((flag & 0x20u) != 0) {
            if (pos + size > prefix.size() || pos + size > schema_end) {
                break;
            }
            pos += size;
        }
        if ((flag & 0x40u) != 0) {
            row_offset += size;
        }
        columns.push_back(std::move(column));
    }
    return columns;
}

void add_utf_vldata_patterns(
    std::vector<HexPatternRange>& out,
    uint64_t total_size,
    std::span<const uint8_t> prefix,
    uint64_t base_offset,
    uint64_t rows_offset,
    uint64_t strings_offset,
    uint64_t data_offset,
    uint16_t num_columns,
    uint16_t row_width,
    uint32_t num_rows,
    std::string_view table_name
) {
    if (row_width == 0 || num_rows == 0 || num_columns == 0) {
        return;
    }
    const auto base = static_cast<size_t>(base_offset);
    const auto columns = utf_pattern_columns(prefix, base, num_columns, rows_offset, strings_offset, data_offset);
    uint64_t added = 0;
    for (uint32_t row = 0; row < num_rows && added < max_entry_patterns; ++row) {
        for (const auto& column : columns) {
            if (added >= max_entry_patterns || column.type != 0x0B || (column.flag & 0x40u) == 0) {
                continue;
            }
            const auto field_offset = base_offset + rows_offset + static_cast<uint64_t>(row) * row_width + column.row_offset;
            if (field_offset + 8 > prefix.size()) {
                continue;
            }
            const auto* field = prefix.data() + static_cast<size_t>(field_offset);
            const auto ref_offset = cricodecs::io::read_be<uint32_t>(field);
            const auto ref_size = cricodecs::io::read_be<uint32_t>(field + 4);
            if (ref_size == 0) {
                continue;
            }
            QString label;
            if (table_name == "AAX" && column.name == "data") {
                label = QStringLiteral("AAX segment %1 ADX payload").arg(row);
            } else {
                label = QStringLiteral("UTF row %1 %2 VLData")
                    .arg(row)
                    .arg(QString::fromStdString(column.name));
            }
            add(out, base_offset + data_offset + ref_offset, ref_size, label, tone(static_cast<int>(added) + 4), total_size);
            ++added;
        }
    }
}

void add_utf_patterns_at(
    std::vector<HexPatternRange>& out,
    uint64_t total_size,
    std::span<const uint8_t> prefix,
    uint64_t base_offset,
    QString label
) {
    if (base_offset > prefix.size() || prefix.size() - static_cast<size_t>(base_offset) < 0x20 ||
        !is_utf_magic(prefix, static_cast<size_t>(base_offset))) {
        return;
    }
    const auto base = static_cast<size_t>(base_offset);
    const auto table_size = cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x04) + 0x08u;
    const auto version = cricodecs::io::read_be<uint16_t>(prefix.data() + base + 0x08);
    const auto rows_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint16_t>(prefix.data() + base + 0x0A)) + 0x08u;
    const auto strings_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x0C)) + 0x08u;
    const auto data_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x10)) + 0x08u;
    const auto table_name_offset = cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x14);
    const auto num_columns = cricodecs::io::read_be<uint16_t>(prefix.data() + base + 0x18);
    const auto row_width = cricodecs::io::read_be<uint16_t>(prefix.data() + base + 0x1A);
    const auto num_rows = cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x1C);
    const auto table_name = utf_string_at(prefix, base, strings_offset, data_offset, table_name_offset);
    const auto table_total = std::min<uint64_t>(table_size, total_size > base_offset ? total_size - base_offset : 0);
    if (table_total < 0x20) {
        return;
    }
    add(out, base_offset, table_total, std::move(label), tone(0), total_size);
    add(out, base_offset, 0x20, QStringLiteral("UTF header"), tone(0), total_size);
    add_field(out, base_offset + 0x00, 4, QStringLiteral("UTF magic"), ascii_value(prefix, base, 4), 0, total_size);
    add_field(out, base_offset + 0x04, 4, QStringLiteral("table size"), hex_value(table_size), 1, total_size);
    add_field(out, base_offset + 0x08, 2, QStringLiteral("version"), QString::number(version), 2, total_size);
    add_field(out, base_offset + 0x0A, 2, QStringLiteral("rows offset"), hex_value(rows_offset), 3, total_size);
    add_field(out, base_offset + 0x0C, 4, QStringLiteral("strings offset"), hex_value(strings_offset), 4, total_size);
    add_field(out, base_offset + 0x10, 4, QStringLiteral("data offset"), hex_value(data_offset), 5, total_size);
    add_field(out, base_offset + 0x18, 2, QStringLiteral("columns"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + base + 0x18)), 6, total_size);
    add_field(out, base_offset + 0x1A, 2, QStringLiteral("row width"), QString::number(row_width), 7, total_size);
    add_field(out, base_offset + 0x1C, 4, QStringLiteral("rows"), QString::number(num_rows), 8, total_size);
    if (rows_offset > 0x20) {
        add(out, base_offset + 0x20, rows_offset - 0x20, QStringLiteral("UTF schema"), tone(1), total_size);
    }
    if (strings_offset > rows_offset) {
        add(out, base_offset + rows_offset, strings_offset - rows_offset, QStringLiteral("UTF rows"), tone(2), total_size);
    }
    if (data_offset > strings_offset) {
        add(out, base_offset + strings_offset, data_offset - strings_offset, QStringLiteral("UTF strings"), tone(3), total_size);
    }
    if (data_offset < table_total) {
        add(out, base_offset + data_offset, table_total - data_offset, QStringLiteral("UTF data"), tone(4), total_size);
    }
    add_utf_vldata_patterns(
        out,
        total_size,
        prefix,
        base_offset,
        rows_offset,
        strings_offset,
        data_offset,
        num_columns,
        row_width,
        num_rows,
        table_name
    );
}

void add_utf_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    add_utf_patterns_at(out, total_size, prefix, 0, QStringLiteral("UTF table"));
}

} // namespace cristudio::hexpatterns
