#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_acx_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix) {
    const auto lower = lower_ascii(format);
    if (lower.find("acx") != std::string::npos) {
        add(out, 0, std::min<uint64_t>(0x20, total_size), QStringLiteral("ACX header"), tone(0), total_size);
        if (prefix.size() >= 8) {
            const auto count = cricodecs::io::read_be<uint32_t>(prefix.data() + 0x04);
            add_field(out, 0x00, 4, QStringLiteral("ACX marker"), hex_value(cricodecs::io::read_be<uint32_t>(prefix.data()), 8), 0, total_size);
            add_field(out, 0x04, 4, QStringLiteral("entry count"), QString::number(count), 1, total_size);
            add(out, 0x08, static_cast<uint64_t>(count) * 8u, QStringLiteral("ACX entry table"), tone(2), total_size);
        }
    }
}

} // namespace cristudio::hexpatterns
