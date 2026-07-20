#include "shared/embedded_preview_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace cristudio {
namespace {

std::string hex_offset(size_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

} // namespace

std::string hex_dump(std::span<const uint8_t> bytes, size_t max_bytes, bool& truncated) {
    const auto shown = std::min(bytes.size(), max_bytes);
    truncated = bytes.size() > shown;

    std::ostringstream out;
    for (size_t offset = 0; offset < shown; offset += 16) {
        const auto row_size = std::min<size_t>(16, shown - offset);
        out << hex_offset(offset) << "  ";
        for (size_t index = 0; index < 16; ++index) {
            if (index < row_size) {
                out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(bytes[offset + index]);
            } else {
                out << "  ";
            }
            out << (index == 7 ? "  " : " ");
        }
        out << " |";
        for (size_t index = 0; index < row_size; ++index) {
            const auto ch = bytes[offset + index];
            out << (ch >= 0x20 && ch < 0x7F ? static_cast<char>(ch) : '.');
        }
        out << "|\n";
    }
    return out.str();
}

bool likely_image_entry(std::string_view name, std::span<const uint8_t> bytes) {
    const auto dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot + 1 < name.size()) {
        std::string ext(name.substr(dot + 1));
        std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "webp") {
            return true;
        }
    }

    return (bytes.size() >= 8 &&
               bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
               bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) ||
           (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) ||
           (bytes.size() >= 6 &&
               bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8') ||
           (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') ||
           (bytes.size() >= 12 &&
               bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
               bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P');
}

} // namespace cristudio
