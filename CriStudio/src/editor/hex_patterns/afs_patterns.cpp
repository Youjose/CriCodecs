#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_afs_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix) {
    const auto lower = lower_ascii(format);
    if (lower.find("afs") != std::string::npos && has(prefix, 0, "AFS")) {
        add(out, 0, 8, QStringLiteral("AFS header"), tone(0), total_size);
        const auto count = prefix.size() >= 8 ? cricodecs::io::read_le<uint32_t>(prefix.data() + 4) : 0;
        add_field(out, 0, 4, QStringLiteral("AFS magic"), ascii_value(prefix, 0, 4), 0, total_size);
        add_field(out, 4, 4, QStringLiteral("AFS entry count"), QString::number(count), 1, total_size);
        add(out, 8, static_cast<uint64_t>(count) * 8u, QStringLiteral("AFS entry table"), tone(1), total_size);
        add(out, 8ull + static_cast<uint64_t>(count) * 8u, 8, QStringLiteral("AFS directory table pointer"), tone(3), total_size);
    }
}

} // namespace cristudio::hexpatterns
