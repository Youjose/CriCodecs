#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

[[nodiscard]] bool is_usm_chunk_magic(std::span<const uint8_t> bytes, size_t offset) {
    return has(bytes, offset, "CRID") || has(bytes, offset, "@SFV") || has(bytes, offset, "@SFA") ||
        has(bytes, offset, "@SBT") || has(bytes, offset, "@ALP") || has(bytes, offset, "@AHX") ||
        has(bytes, offset, "@ELM") || has(bytes, offset, "@ATP") || has(bytes, offset, "@PST") ||
        has(bytes, offset, "@CUE") || has(bytes, offset, "@STA") || has(bytes, offset, "@USR") ||
        has(bytes, offset, "SFSH");
}

void add_usm_chunk_patterns_from_header(
    std::vector<HexPatternRange>& out,
    uint64_t total_size,
    std::span<const uint8_t, 0x20> header,
    uint64_t base_offset
) {
    if (!is_usm_chunk_magic(header, 0)) {
        return;
    }
    const auto chunk_size = cricodecs::io::read_be<uint32_t>(header.data() + 0x04);
    const auto header_offset = header[0x09];
    const auto packed_size = static_cast<uint64_t>(chunk_size) + 0x08u;
    add(out, base_offset, std::min<uint64_t>(packed_size, total_size > base_offset ? total_size - base_offset : 0),
        QStringLiteral("USM chunk %1").arg(ascii_value(header, 0, 4)), tone(0), total_size);
    add(out, base_offset, 0x20, QStringLiteral("USM chunk header"), tone(0), total_size);
    add_field(out, base_offset + 0x00, 4, QStringLiteral("chunk.magic"), ascii_value(header, 0, 4), 0, total_size);
    add_field(out, base_offset + 0x04, 4, QStringLiteral("chunk.size"), QString::number(chunk_size), 1, total_size);
    add_field(out, base_offset + 0x08, 1, QStringLiteral("chunk.unk08"), hex_value(header[0x08], 2), 2, total_size);
    add_field(out, base_offset + 0x09, 1, QStringLiteral("chunk.offset"), QString::number(header_offset), 3, total_size);
    add_field(out, base_offset + 0x0A, 2, QStringLiteral("chunk.padding"), QString::number(cricodecs::io::read_be<uint16_t>(header.data() + 0x0A)), 4, total_size);
    add_field(out, base_offset + 0x0C, 1, QStringLiteral("chunk.channel"), QString::number(header[0x0C]), 5, total_size);
    add_field(out, base_offset + 0x0D, 1, QStringLiteral("chunk.unk0d"), hex_value(header[0x0D], 2), 6, total_size);
    add_field(out, base_offset + 0x0E, 1, QStringLiteral("chunk.unk0e"), hex_value(header[0x0E], 2), 7, total_size);
    add_field(out, base_offset + 0x0F, 1, QStringLiteral("chunk.payload_type"), hex_value(header[0x0F], 2), 8, total_size);
    add_field(out, base_offset + 0x10, 4, QStringLiteral("chunk.frame_time"), QString::number(cricodecs::io::read_be<uint32_t>(header.data() + 0x10)), 9, total_size);
    add_field(out, base_offset + 0x14, 4, QStringLiteral("chunk.frame_rate"), QString::number(cricodecs::io::read_be<uint32_t>(header.data() + 0x14)), 10, total_size);
    add_field(out, base_offset + 0x18, 4, QStringLiteral("chunk.unk18"), hex_value(cricodecs::io::read_be<uint32_t>(header.data() + 0x18), 8), 11, total_size);
    add_field(out, base_offset + 0x1C, 4, QStringLiteral("chunk.unk1c"), hex_value(cricodecs::io::read_be<uint32_t>(header.data() + 0x1C), 8), 12, total_size);
    if (chunk_size > header_offset) {
        const auto payload_offset = base_offset + 0x08u + header_offset;
        add(out, payload_offset, chunk_size - header_offset, QStringLiteral("USM chunk payload"), tone(2), total_size);
    }
}

void add_usm_chunk_patterns_at(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix, uint64_t base_offset) {
    if (base_offset > prefix.size() || prefix.size() - static_cast<size_t>(base_offset) < 0x20 ||
        !is_usm_chunk_magic(prefix, static_cast<size_t>(base_offset))) {
        return;
    }
    const auto base = static_cast<size_t>(base_offset);
    add_usm_chunk_patterns_from_header(out, total_size, std::span<const uint8_t, 0x20>(prefix.data() + base, 0x20), base_offset);
    const auto chunk_size = cricodecs::io::read_be<uint32_t>(prefix.data() + base + 0x04);
    const auto header_offset = prefix[base + 0x09];
    if (chunk_size > header_offset) {
        const auto payload_offset = base_offset + 0x08u + header_offset;
        add_utf_patterns_at(out, total_size, prefix, payload_offset,
            QStringLiteral("USM %1 UTF payload").arg(ascii_value(prefix, base, 4)));
    }
}

void add_usm_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    size_t offset = 0;
    for (int chunk_count = 0; offset + 0x20 <= prefix.size() && chunk_count < 4096; ++chunk_count) {
        if (!is_usm_chunk_magic(prefix, offset)) {
            break;
        }
        add_usm_chunk_patterns_at(out, total_size, prefix, offset);
        const auto chunk_size = cricodecs::io::read_be<uint32_t>(prefix.data() + offset + 0x04);
        const auto packed_size = static_cast<uint64_t>(chunk_size) + 0x08u;
        if (packed_size < 0x20 || packed_size > static_cast<uint64_t>(prefix.size() - offset)) {
            break;
        }
        offset += static_cast<size_t>(packed_size);
    }
}

} // namespace cristudio::hexpatterns
