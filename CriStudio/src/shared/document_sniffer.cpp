#include "shared/document_sniffer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace cristudio {
namespace {

uint32_t be32(std::span<const uint8_t> bytes, size_t offset = 0) {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return (static_cast<uint32_t>(bytes[offset]) << 24) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<uint32_t>(bytes[offset + 3]);
}

bool has_magic_at(std::span<const uint8_t> bytes, size_t offset, std::string_view magic) {
    return bytes.size() >= offset + magic.size() &&
           std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset), [](char expected, uint8_t actual) {
               return static_cast<uint8_t>(expected) == actual;
           });
}

bool contains_ascii(std::span<const uint8_t> bytes, std::string_view needle) {
    return needle.empty() ||
           std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
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

} // namespace

std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
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

std::vector<std::string> sniff_format_order(std::span<const uint8_t> bytes, bool include_riff_wave) {
    std::vector<std::string> order;
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
        const auto sniff_bytes = bytes.first(std::min(bytes.size(), file_sniff_prefix_size));
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
    if (has_magic_at(bytes, 0, std::string_view("\x00\x00\x01\xBA", 4))) {
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
    return order;
}

std::vector<std::string> sniff_format_order(const std::filesystem::path& path, bool include_riff_wave) {
    std::array<uint8_t, file_sniff_prefix_size> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const auto read_size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    return sniff_format_order(std::span<const uint8_t>(header.data(), read_size), include_riff_wave);
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
    if (lower_ascii(path.extension().generic_string()) == ".sbt") {
        order.insert(order.begin(), "sbt");
    }
    apply_utf_family_hint(order, lower_ascii(path.filename().generic_string()));
    return order;
}

} // namespace cristudio
