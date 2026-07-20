#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_riff_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 12 || !has(prefix, 0, "RIFF")) {
        return;
    }
    add(out, 0, 12, QStringLiteral("RIFF header"), tone(0), total_size);
    add_field(out, 0x00, 4, QStringLiteral("RIFF magic"), ascii_value(prefix, 0, 4), 0, total_size);
    add_field(out, 0x04, 4, QStringLiteral("RIFF size"), QString::number(cricodecs::io::read_le<uint32_t>(prefix.data() + 0x04)), 1, total_size);
    add_field(out, 0x08, 4, QStringLiteral("RIFF type"), ascii_value(prefix, 8, 4), 2, total_size);
    size_t offset = 12;
    int index = 1;
    while (offset + 8 <= prefix.size() && index < 64) {
        const uint32_t size = cricodecs::io::read_le<uint32_t>(prefix.data() + offset + 4);
        QString name = QString::fromLatin1(reinterpret_cast<const char*>(prefix.data() + offset), 4);
        const uint64_t chunk_size = 8ull + size + (size & 1u);
        add(out, offset, chunk_size, QStringLiteral("RIFF %1 chunk").arg(name), tone(index), total_size);
        add_field(out, offset, 4, QStringLiteral("RIFF chunk id"), name, index, total_size);
        add_field(out, offset + 4, 4, QStringLiteral("RIFF chunk size"), QString::number(size), index + 1, total_size);
        if (name == QStringLiteral("fmt ") && offset + 24 <= prefix.size()) {
            add_field(out, offset + 8, 2, QStringLiteral("fmt.audio_format"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + offset + 8)), index + 2, total_size);
            add_field(out, offset + 10, 2, QStringLiteral("fmt.channels"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + offset + 10)), index + 3, total_size);
            add_field(out, offset + 12, 4, QStringLiteral("fmt.sample_rate"), QString::number(cricodecs::io::read_le<uint32_t>(prefix.data() + offset + 12)), index + 4, total_size);
            add_field(out, offset + 16, 4, QStringLiteral("fmt.byte_rate"), QString::number(cricodecs::io::read_le<uint32_t>(prefix.data() + offset + 16)), index + 5, total_size);
            add_field(out, offset + 20, 2, QStringLiteral("fmt.block_align"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + offset + 20)), index + 6, total_size);
            add_field(out, offset + 22, 2, QStringLiteral("fmt.bits_per_sample"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + offset + 22)), index + 7, total_size);
        }
        if (chunk_size == 0 || offset + chunk_size <= offset) {
            break;
        }
        offset += static_cast<size_t>(chunk_size);
        ++index;
    }
}

} // namespace cristudio::hexpatterns
