#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_aax_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size, std::span<const uint8_t> prefix) {
    const auto lower = lower_ascii(format);
    if (lower.find("aax") != std::string::npos || has(prefix, 0, "AAX")) {
        add(out, 0, std::min<uint64_t>(0x40, total_size), QStringLiteral("AAX header"), tone(0), total_size);
    }
}

} // namespace cristudio::hexpatterns
