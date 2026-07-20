#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

#include <array>

namespace cristudio::hexpatterns {

[[nodiscard]] bool is_cpk_chunk_magic(std::span<const uint8_t> bytes, size_t offset) {
    return has(bytes, offset, "CPK ") || has(bytes, offset, "TOC ") || has(bytes, offset, "ITOC") ||
        has(bytes, offset, "GTOC") || has(bytes, offset, "ETOC") || has(bytes, offset, "HTOC");
}

void add_cpk_chunk_patterns_at(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix, uint64_t base_offset) {
    if (base_offset > prefix.size() || prefix.size() - static_cast<size_t>(base_offset) < 0x10 ||
        !is_cpk_chunk_magic(prefix, static_cast<size_t>(base_offset))) {
        return;
    }
    const auto base = static_cast<size_t>(base_offset);
    const auto utf_size = cricodecs::io::read_le<uint32_t>(prefix.data() + base + 0x08);
    const auto chunk_size = 0x10ull + utf_size;
    const auto enc_flag = cricodecs::io::read_le<uint32_t>(prefix.data() + base + 0x04);
    add(out, base_offset, chunk_size, QStringLiteral("CPK chunk %1").arg(ascii_value(prefix, base, 4)), tone(0), total_size);
    add(out, base_offset, 0x10, QStringLiteral("CPK chunk header"), tone(0), total_size);
    add_field(out, base_offset + 0x00, 4, QStringLiteral("chunk.magic"), ascii_value(prefix, base, 4), 0, total_size);
    add_field(out, base_offset + 0x04, 4, QStringLiteral("UTF encryption flag"), hex_value(enc_flag, 8), 1, total_size);
    add_field(out, base_offset + 0x08, 4, QStringLiteral("UTF payload size"), QString::number(utf_size), 2, total_size);
    add_field(out, base_offset + 0x0C, 4, QStringLiteral("reserved"), hex_value(cricodecs::io::read_le<uint32_t>(prefix.data() + base + 0x0C), 8), 3, total_size);
    if (utf_size != 0) {
        const auto payload_offset = base_offset + 0x10;
        add(out, payload_offset, utf_size,
            enc_flag == 0xFFu ? QStringLiteral("clear UTF payload") : QStringLiteral("encrypted UTF payload"),
            tone(2),
            total_size);
        add_utf_patterns_at(out, total_size, prefix, payload_offset,
            QStringLiteral("CPK %1 UTF payload").arg(ascii_value(prefix, base, 4)));
    }
}

void add_cpk_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    add_cpk_chunk_patterns_at(out, total_size, prefix, 0);
}

void add_cpk_document_patterns(std::vector<HexPatternRange>& out, const LoadedDocument& document) {
    const auto lower = lower_ascii(document.format);
    if (lower.find("cpk") == std::string::npos) {
        return;
    }
    struct ChunkInfo {
        std::string_view name;
        const char* magic;
        int color;
    };
    static constexpr std::array chunks = {
        ChunkInfo{"Toc", "TOC ", 9},
        ChunkInfo{"Itoc", "ITOC", 10},
        ChunkInfo{"Gtoc", "GTOC", 11},
        ChunkInfo{"Etoc", "ETOC", 12},
    };
    for (const auto& chunk : chunks) {
        const auto offset = info_value(document, std::string(chunk.name) + " offset");
        const auto size = info_value(document, std::string(chunk.name) + " size");
        if (!offset || !size || *size == 0) {
            continue;
        }
        const auto total = *size + 0x10u;
        add(out, *offset, total, QStringLiteral("CPK chunk %1").arg(QLatin1String(chunk.magic, 4)), tone(chunk.color), document.file_size);
        add(out, *offset, 0x10, QStringLiteral("CPK chunk header"), tone(chunk.color), document.file_size);
        add(out, *offset + 0x10u, *size, QStringLiteral("CPK UTF payload"), tone(chunk.color + 1), document.file_size);
    }
}

} // namespace cristudio::hexpatterns
