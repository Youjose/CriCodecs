#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_sfd_patterns(std::vector<HexPatternRange>& out, std::string_view format, uint64_t total_size) {
    const auto lower = lower_ascii(format);
    if (lower.find("sfd") != std::string::npos || lower.find("sofdec") != std::string::npos) {
        add(out, 0, std::min<uint64_t>(0x800, total_size), QStringLiteral("SFD/SofDec header packets"), tone(0), total_size);
    }
}

} // namespace cristudio::hexpatterns
