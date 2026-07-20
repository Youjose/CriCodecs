#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

#include <algorithm>

namespace cristudio::hexpatterns {

void add_aix_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 0x20 || !has(prefix, 0, "AIXF")) {
        return;
    }
    const auto data_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x04)) + 0x08u;
    const auto segment_count = cricodecs::io::read_be<uint16_t>(prefix.data() + 0x18);
    const auto segment_table_offset = 0x20ull;
    const auto segment_table_size = static_cast<uint64_t>(segment_count) * 0x10ull;
    const auto subtable_offset = segment_table_offset + segment_table_size;
    add(out, 0, 0x20, QStringLiteral("AIXF header"), tone(0), total_size);
    add_field(out, 0x00, 4, QStringLiteral("AIX magic"), ascii_value(prefix, 0, 4), 0, total_size);
    add_field(out, 0x04, 4, QStringLiteral("data offset"), hex_value(data_offset), 1, total_size);
    add_field(out, 0x08, 4, QStringLiteral("version"), hex_value(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x08), 8), 2, total_size);
    add_field(out, 0x0C, 4, QStringLiteral("header size"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x0C)), 3, total_size);
    add_field(out, 0x18, 2, QStringLiteral("segment count"), QString::number(segment_count), 4, total_size);
    add(out, segment_table_offset, segment_table_size, QStringLiteral("AIX segment table"), tone(1), total_size);
    for (uint16_t index = 0; index < segment_count; ++index) {
        const auto entry = segment_table_offset + static_cast<uint64_t>(index) * 0x10ull;
        if (entry + 0x10 > prefix.size()) {
            break;
        }
        add_field(out, entry + 0x00, 4, QStringLiteral("segment %1 offset").arg(index), hex_value(cricodecs::io::read_be<uint32_t>(prefix.data() + entry)), 5, total_size);
        add_field(out, entry + 0x04, 4, QStringLiteral("segment %1 size").arg(index), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + entry + 4)), 6, total_size);
        add_field(out, entry + 0x08, 4, QStringLiteral("segment %1 samples").arg(index), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + entry + 8)), 7, total_size);
        add_field(out, entry + 0x0C, 4, QStringLiteral("segment %1 sample_rate").arg(index), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + entry + 12)), 8, total_size);
    }
    if (subtable_offset + 0x18 <= prefix.size()) {
        add(out, subtable_offset, data_offset > subtable_offset ? data_offset - subtable_offset : 0, QStringLiteral("AIX layer metadata"), tone(2), total_size);
        const auto layer_list = subtable_offset + 0x10ull;
        const auto layer_count = prefix[static_cast<size_t>(layer_list)];
        add_field(out, layer_list, 1, QStringLiteral("layer count"), QString::number(layer_count), 9, total_size);
        add(out, layer_list + 0x08, static_cast<uint64_t>(layer_count) * 0x08ull, QStringLiteral("AIX layer table"), tone(3), total_size);
        for (uint8_t index = 0; index < layer_count; ++index) {
            const auto entry = layer_list + 0x08ull + static_cast<uint64_t>(index) * 0x08ull;
            if (entry + 0x08 > prefix.size()) {
                break;
            }
            add_field(out, entry + 0x00, 4, QStringLiteral("layer %1 sample_rate").arg(index), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + entry)), 10, total_size);
            add_field(out, entry + 0x04, 4, QStringLiteral("layer %1 channels").arg(index), QString::number(cricodecs::io::read_le<uint32_t>(prefix.data() + entry + 4)), 11, total_size);
        }
    }
    for (size_t offset = static_cast<size_t>(data_offset); offset + 0x10 <= prefix.size();) {
        if (!(has(prefix, offset, "AIXP") || has(prefix, offset, "AIXE"))) {
            break;
        }
        const auto block_size = static_cast<uint64_t>(cricodecs::io::read_be<uint32_t>(prefix.data() + offset + 0x04)) + 0x08u;
        add(out, offset, block_size, QStringLiteral("AIX block %1").arg(ascii_value(prefix, offset, 4)), tone(12), total_size);
        add_field(out, offset + 0x00, 4, QStringLiteral("block magic"), ascii_value(prefix, offset, 4), 12, total_size);
        add_field(out, offset + 0x04, 4, QStringLiteral("block size"), QString::number(block_size), 13, total_size);
        if (has(prefix, offset, "AIXP")) {
            add_field(out, offset + 0x08, 1, QStringLiteral("layer index"), QString::number(prefix[offset + 0x08]), 14, total_size);
            add_field(out, offset + 0x09, 1, QStringLiteral("layer count"), QString::number(prefix[offset + 0x09]), 15, total_size);
            add_field(out, offset + 0x0A, 2, QStringLiteral("payload size"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + offset + 0x0A)), 16, total_size);
            add_field(out, offset + 0x0C, 4, QStringLiteral("sequence"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + offset + 0x0C)), 17, total_size);
        }
        if (block_size < 8 || offset + block_size <= offset) {
            break;
        }
        offset += static_cast<size_t>(block_size);
    }
}

void add_aix_document_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document) {
    const auto lower = lower_ascii(document.format);
    if (lower.find("aix") == std::string::npos) {
        return;
    }
    std::vector<uint64_t> highlighted_offsets;
    for (const auto& entry : document.entries) {
        const auto offset = decimal_prefix(entry.offset);
        const auto size = byte_size_prefix(entry.size);
        if (!offset || !size || *size == 0) {
            continue;
        }
        if (std::ranges::find(highlighted_offsets, *offset) != highlighted_offsets.end()) {
            continue;
        }
        highlighted_offsets.push_back(*offset);
        add(
            out,
            *offset,
            *size,
            QStringLiteral("AIX %1").arg(QString::fromStdString(entry.name)),
            tone(static_cast<int>(entry.source_index) + 4),
            document.file_size
        );
    }
}

} // namespace cristudio::hexpatterns
