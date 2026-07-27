#include "shared/document_sniffer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>

namespace cristudio {
namespace {

constexpr uint32_t max_reasonable_acx_entries = 0x10000;
constexpr size_t utf_hint_prefix_size = 0x804;

uint32_t be32(std::span<const uint8_t> bytes, size_t offset = 0) {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return (static_cast<uint32_t>(bytes[offset]) << 24) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<uint32_t>(bytes[offset + 3]);
}

uint32_t le32(std::span<const uint8_t> bytes, size_t offset) {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

bool has_magic_at(std::span<const uint8_t> bytes, size_t offset, std::string_view magic) {
    return bytes.size() >= offset + magic.size() &&
           std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset), [](char expected, uint8_t actual) {
               return static_cast<uint8_t>(expected) == actual;
           });
}

std::optional<size_t> acx_table_size(std::span<const uint8_t> bytes, uint64_t source_size) {
    if (bytes.size() < 8 || be32(bytes) != 0) {
        return std::nullopt;
    }

    const uint32_t entry_count = be32(bytes, 4);
    if (entry_count == 0 || entry_count > max_reasonable_acx_entries) {
        return std::nullopt;
    }

    const uint64_t table_size = 8ull + static_cast<uint64_t>(entry_count) * 8ull;
    if (table_size > source_size || table_size > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<size_t>(table_size);
}

bool has_acx_table(std::span<const uint8_t> bytes, uint64_t source_size) {
    const auto table_size = acx_table_size(bytes, source_size);
    if (!table_size || bytes.size() < *table_size) {
        return false;
    }

    const uint32_t entry_count = be32(bytes, 4);
    for (uint32_t index = 0; index < entry_count; ++index) {
        const size_t row = 8u + static_cast<size_t>(index) * 8u;
        const uint64_t offset = be32(bytes, row);
        const uint64_t size = be32(bytes, row + 4u);
        if (offset > source_size || size > source_size - offset) {
            return false;
        }
        if (size != 0 && offset < *table_size) {
            return false;
        }
    }
    return true;
}

bool contains_ascii(std::span<const uint8_t> bytes, std::string_view needle) {
    return needle.empty() ||
           std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

bool has_sbt_records(std::span<const uint8_t> bytes, uint64_t source_size) {
    constexpr uint64_t record_header_size = 0x14;
    if (source_size < record_header_size || bytes.size() < record_header_size) {
        return false;
    }

    uint64_t offset = 0;
    bool found_record = false;
    while (offset < source_size) {
        if (source_size - offset < record_header_size) {
            return false;
        }
        if (offset + record_header_size > bytes.size()) {
            return found_record;
        }

        const auto row = static_cast<size_t>(offset);
        const uint32_t time_unit = le32(bytes, row + 0x04);
        const uint64_t text_size = le32(bytes, row + 0x10);
        if (time_unit == 0 || text_size > source_size - offset - record_header_size) {
            return false;
        }

        found_record = true;
        offset += record_header_size + text_size;
        if (offset > bytes.size() && offset < source_size) {
            return true;
        }
    }
    return found_record && offset == source_size;
}

void move_to_front(std::vector<std::string>& order, std::string_view type) {
    const auto it = std::ranges::find(order, type);
    if (it == order.end() || it == order.begin()) {
        return;
    }
    auto value = *it;
    order.erase(it);
    order.insert(order.begin(), std::move(value));
}

bool has_ordered_type(const std::vector<std::string>& order, std::string_view type) {
    return std::ranges::find(order, type) != order.end();
}

void apply_utf_family_hint(std::vector<std::string>& order, std::string_view hint) {
    if (!has_ordered_type(order, "utf")) {
        return;
    }

    if (hint.find(".acb") != std::string_view::npos ||
        hint.find("acb") != std::string_view::npos) {
        move_to_front(order, "acb");
    } else if (hint.find(".csb") != std::string_view::npos ||
               hint.find("csb") != std::string_view::npos) {
        move_to_front(order, "csb");
    } else if (hint.find(".aax") != std::string_view::npos ||
               hint.find("aax") != std::string_view::npos) {
        move_to_front(order, "aax");
    } else if (hint.find(".utf") != std::string_view::npos ||
               hint.find("utf") != std::string_view::npos) {
        move_to_front(order, "utf");
    }
}

std::vector<std::string> sniff_format_order_impl(
    std::span<const uint8_t> bytes,
    uint64_t source_size,
    bool include_riff_wave
) {
    std::vector<std::string> order;
    if (has_acx_table(bytes, source_size)) {
        order.push_back("acx");
    }
    if (has_cvm_header(bytes)) {
        order.push_back("cvm");
    }
    if (has_magic_at(bytes, 0, "CPK ")) {
        order.push_back("cpk");
    }
    if (has_magic_at(bytes, 0, "CRID") || has_magic_at(bytes, 0, "SFSH")) {
        order.push_back("usm");
    }
    if (has_magic_at(bytes, 0, "AFS2")) {
        order.push_back("awb");
    }
    if (has_magic_at(bytes, 0, std::string_view("AFS\0", 4))) {
        order.push_back("afs");
    }
    if (has_magic_at(bytes, 0, "AIXF")) {
        order.push_back("aix");
    }
    if (has_magic_at(bytes, 0, "@UTF")) {
        order.insert(order.end(), {"csb", "acb", "aax", "utf"});
        const auto sniff_bytes = bytes.first(std::min(bytes.size(), utf_hint_prefix_size));
        if (contains_ascii(sniff_bytes, "CueNameTable") ||
            contains_ascii(sniff_bytes, "WaveformTable") ||
            contains_ascii(sniff_bytes, "AwbFile")) {
            move_to_front(order, "acb");
        } else if (contains_ascii(sniff_bytes, "SOUND_ELEMENT") ||
                   contains_ascii(sniff_bytes, "TBLSDL")) {
            move_to_front(order, "csb");
        } else if (contains_ascii(sniff_bytes, "AAX")) {
            move_to_front(order, "aax");
        }
    }
    if (has_sfd_signature(bytes)) {
        order.push_back("sfd");
    }
    if (bytes.size() >= 4 &&
        bytes[0] == 0x80 &&
        bytes[1] == 0x00) {
        order.push_back("adx");
    }
    if (has_hca_signature(bytes)) {
        order.push_back("hca");
    }
    if (include_riff_wave &&
        bytes.size() >= 12 &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'A' && bytes[10] == 'V' && bytes[11] == 'E') {
        order.push_back("wav");
    }
    if (order.empty() && has_sbt_records(bytes, source_size)) {
        order.push_back("sbt");
    }
    return order;
}

} // namespace

std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool has_acx_header(std::span<const uint8_t> bytes) {
    return has_acx_table(bytes, bytes.size());
}

bool has_cvm_header(std::span<const uint8_t> bytes) {
    return has_magic_at(bytes, 0, "CVMH") && has_magic_at(bytes, 0x800, "ZONE");
}

bool has_cvm_header(const std::filesystem::path& path) {
    std::array<uint8_t, 0x804> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const auto read_size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    return has_cvm_header(std::span<const uint8_t>(header.data(), read_size));
}

bool has_hca_signature(std::span<const uint8_t> bytes) {
    return bytes.size() >= 4 && (be32(bytes) & 0x7F7F7F7Fu) == 0x48434100u;
}

bool has_sfd_signature(std::span<const uint8_t> bytes) {
    constexpr std::string_view pack_start_code("\x00\x00\x01\xBA", 4);
    const auto sniff_bytes = bytes.first(std::min(bytes.size(), file_sniff_prefix_size));
    return has_magic_at(bytes, 0, "Sofdec Stream") ||
           has_magic_at(bytes, 0, "SofdecStream") ||
           std::search(
               sniff_bytes.begin(),
               sniff_bytes.end(),
               pack_start_code.begin(),
               pack_start_code.end(),
               [](uint8_t actual, char expected) {
                   return actual == static_cast<uint8_t>(expected);
               }
           ) != sniff_bytes.end();
}

std::vector<std::string> sniff_format_order(std::span<const uint8_t> bytes, bool include_riff_wave) {
    return sniff_format_order_impl(bytes, bytes.size(), include_riff_wave);
}

std::vector<std::string> sniff_format_order(const std::filesystem::path& path, bool include_riff_wave) {
    std::error_code size_error;
    const auto source_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        return {};
    }

    std::array<uint8_t, file_sniff_prefix_size> header;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.read(
        reinterpret_cast<char*>(header.data()),
        static_cast<std::streamsize>(header.size())
    );
    size_t read_size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    auto header_bytes = std::span<const uint8_t>(header.data(), read_size);
    auto order = sniff_format_order_impl(header_bytes, source_size, include_riff_wave);

    const auto table_size = acx_table_size(header_bytes, source_size);
    if (table_size && *table_size > header_bytes.size()) {
        std::vector<uint8_t> table(*table_size);
        input.clear();
        input.seekg(0, std::ios::beg);
        input.read(
            reinterpret_cast<char*>(table.data()),
            static_cast<std::streamsize>(table.size())
        );
        if (static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0)) == table.size() &&
            has_acx_table(table, source_size)) {
            order.insert(order.begin(), "acx");
            return order;
        }
    }

    return order;
}

std::vector<std::string> sniff_embedded_format_order(
    std::span<const uint8_t> bytes,
    std::string_view name,
    std::string_view type,
    std::string_view source_format,
    std::string_view nested_source_format
) {
    auto order = sniff_format_order(bytes);

    const auto lower_source = lower_ascii(
        std::string(name) + " " +
        std::string(type) + " " +
        std::string(source_format) + " " +
        std::string(nested_source_format)
    );
    if (order.empty() && lower_source.find("sbt") != std::string::npos) {
        order.push_back("sbt");
    } else {
        apply_utf_family_hint(order, lower_ascii(std::string(name) + " " + std::string(type)));
    }
    return order;
}

std::vector<std::string> sniff_file_format_order(const std::filesystem::path& path) {
    auto order = sniff_format_order(path, false);
    apply_utf_family_hint(order, lower_ascii(path.filename().generic_string()));
    return order;
}

} // namespace cristudio
