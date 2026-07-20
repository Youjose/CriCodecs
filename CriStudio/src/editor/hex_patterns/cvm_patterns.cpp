#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_cvm_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 0x1000 || !has(prefix, 0, "CVMH") || !has(prefix, 0x800, "ZONE")) {
        return;
    }
    constexpr uint64_t sector = 0x800;
    const auto total = cricodecs::io::read_be<uint64_t>(prefix.data() + 0x1C);
    const auto flags = cricodecs::io::read_be<uint32_t>(prefix.data() + 0x30);
    const auto sector_count = cricodecs::io::read_be<uint32_t>(prefix.data() + 0x80);
    const auto iso_sector = cricodecs::io::read_be<uint32_t>(prefix.data() + 0x88);
    const auto iso_offset = static_cast<uint64_t>(iso_sector) * sector;
    const auto iso_length = cricodecs::io::read_be<uint64_t>(prefix.data() + 0x830);
    add(out, 0, sector, QStringLiteral("CVMH header sector"), tone(0), total_size);
    add_field(out, 0x00, 4, QStringLiteral("CVMH magic"), ascii_value(prefix, 0, 4), 0, total_size);
    add_field(out, 0x04, 8, QStringLiteral("CVMH chunk length"), QString::number(cricodecs::io::read_be<uint64_t>(prefix.data() + 0x04)), 1, total_size);
    add_field(out, 0x1C, 8, QStringLiteral("CVM total size"), QString::number(total), 2, total_size);
    add_field(out, 0x30, 4, QStringLiteral("flags"), hex_value(flags, 8), 3, total_size);
    add_field(out, 0x34, 4, QStringLiteral("filesystem id"), ascii_value(prefix, 0x34, 4), 4, total_size);
    add_field(out, 0x38, 64, QStringLiteral("maker id"), ascii_value(prefix, 0x38, 64).trimmed(), 5, total_size);
    add_field(out, 0x80, 4, QStringLiteral("sector table entries"), QString::number(sector_count), 6, total_size);
    add_field(out, 0x84, 4, QStringLiteral("zone sector index"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x84)), 7, total_size);
    add_field(out, 0x88, 4, QStringLiteral("ISO start sector"), QString::number(iso_sector), 8, total_size);
    add(out, 0x100, static_cast<uint64_t>(sector_count) * 4u, QStringLiteral("CVM sector table"), tone(2), total_size);

    add(out, 0x800, sector, QStringLiteral("ZONE header sector"), tone(9), total_size);
    add_field(out, 0x800, 4, QStringLiteral("ZONE magic"), ascii_value(prefix, 0x800, 4), 9, total_size);
    add_field(out, 0x804, 8, QStringLiteral("ZONE chunk length"), QString::number(cricodecs::io::read_be<uint64_t>(prefix.data() + 0x804)), 10, total_size);
    add_field(out, 0x80C, 4, QStringLiteral("ZONE sector"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x80C)), 11, total_size);
    add_field(out, 0x81C, 4, QStringLiteral("sector length"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x81C)), 12, total_size);
    add_field(out, 0x82C, 4, QStringLiteral("ISO sector"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 0x82C)), 13, total_size);
    add_field(out, 0x830, 8, QStringLiteral("ISO length"), QString::number(iso_length), 14, total_size);
    if (iso_offset < total_size) {
        add(out, iso_offset, iso_length, QStringLiteral("embedded ISO9660 image"), tone(4), total_size);
        const auto pvd_offset = iso_offset + 16ull * sector;
        if (pvd_offset + sector <= prefix.size() && prefix[static_cast<size_t>(pvd_offset)] == 0x01 && has(prefix, static_cast<size_t>(pvd_offset + 1), "CD001")) {
            add(out, pvd_offset, sector, QStringLiteral("ISO9660 primary volume descriptor"), tone(5), total_size);
            add_field(out, pvd_offset + 0, 1, QStringLiteral("PVD type"), QString::number(prefix[static_cast<size_t>(pvd_offset)]), 15, total_size);
            add_field(out, pvd_offset + 1, 5, QStringLiteral("PVD id"), ascii_value(prefix, static_cast<size_t>(pvd_offset + 1), 5), 16, total_size);
            add_field(out, pvd_offset + 8, 32, QStringLiteral("system id"), ascii_value(prefix, static_cast<size_t>(pvd_offset + 8), 32).trimmed(), 17, total_size);
            add_field(out, pvd_offset + 40, 32, QStringLiteral("volume id"), ascii_value(prefix, static_cast<size_t>(pvd_offset + 40), 32).trimmed(), 18, total_size);
        }
    }
}

} // namespace cristudio::hexpatterns
