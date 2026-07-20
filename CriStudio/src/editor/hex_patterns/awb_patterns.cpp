#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_awb_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix) {
    const auto lower = lower_ascii(format);
    if ((lower.find("awb") != std::string::npos || has(prefix, 0, "AFS2")) && has(prefix, 0, "AFS2")) {
        add(out, 0, 0x10, QStringLiteral("AWB/AFS2 header"), tone(0), total_size);
        if (prefix.size() >= 0x10) {
            const auto id_size = cricodecs::io::read_le<uint16_t>(prefix.data() + 0x06);
            const auto count = cricodecs::io::read_le<uint32_t>(prefix.data() + 0x08);
            const auto table_size = static_cast<uint64_t>(id_size) * count + static_cast<uint64_t>(prefix[0x05]) * (static_cast<uint64_t>(count) + 1);
            add_field(out, 0x00, 4, QStringLiteral("AFS2 magic"), ascii_value(prefix, 0, 4), 0, total_size);
            add_field(out, 0x04, 1, QStringLiteral("version"), QString::number(prefix[0x04]), 1, total_size);
            add_field(out, 0x05, 1, QStringLiteral("offset size"), QString::number(prefix[0x05]), 2, total_size);
            add_field(out, 0x06, 2, QStringLiteral("id size"), QString::number(id_size), 3, total_size);
            add_field(out, 0x08, 4, QStringLiteral("entry count"), QString::number(count), 4, total_size);
            add_field(out, 0x0C, 2, QStringLiteral("alignment"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + 0x0C)), 5, total_size);
            add_field(out, 0x0E, 2, QStringLiteral("subkey"), QString::number(cricodecs::io::read_le<uint16_t>(prefix.data() + 0x0E)), 6, total_size);
            add(out, 0x10, table_size, QStringLiteral("AWB id/offset tables"), tone(1), total_size);
        }
    }
}

} // namespace cristudio::hexpatterns
