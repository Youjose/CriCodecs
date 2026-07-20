#include "shared/document_helpers.hpp"

#include "path_text.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace cristudio {

std::string generic_path(const std::filesystem::path& path) {
    return path_to_utf8(path);
}

std::string filename_of(const std::filesystem::path& path) {
    auto name = filename_to_utf8(path);
    return name.empty() ? generic_path(path) : name;
}

std::string display_path_separators(std::string text) {
    std::ranges::replace(text, '\\', '/');
    return text;
}

std::string archive_display_path(std::string_view text) {
    return display_path_separators(std::string(text));
}

std::string archive_leaf_name(std::string_view text) {
    auto path = archive_display_path(text);
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < path.size()) {
        return path.substr(slash + 1);
    }
    return path;
}

std::string lower_extension_text(std::string_view name) {
    auto normalized = archive_display_path(name);
    const auto slash = normalized.find_last_of('/');
    const auto dot = normalized.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    auto ext = normalized.substr(dot);
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

std::string bool_text(bool value) {
    return value ? "yes" : "no";
}

std::string number(uint64_t value) {
    return std::to_string(value);
}

std::string byte_count(uint64_t value) {
    std::ostringstream out;
    out << value << " bytes";
    return out.str();
}

std::string indexed_label(std::string_view label, uint64_t value) {
    std::string text(label);
    text.push_back(' ');
    text += number(value);
    return text;
}

std::string float_text(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

void add_source_info(LoadedDocument& doc) {
    doc.info.push_back({"Path", generic_path(doc.path)});
    doc.info.push_back({"Size", byte_count(doc.file_size)});
}

LoadedDocument base_document(const std::filesystem::path& path, std::string format) {
    LoadedDocument doc;
    doc.path = path;
    doc.display_name = filename_of(path);
    doc.format = std::move(format);
    std::error_code ec;
    doc.file_size = std::filesystem::file_size(path, ec);
    add_source_info(doc);
    return doc;
}

} // namespace cristudio
