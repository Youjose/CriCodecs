#pragma once

#include <QString>

#include <filesystem>
#include <string>
#include <string_view>

namespace cristudio {

inline std::string u8string_to_string(std::u8string text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        text.size()
    };
}

inline std::string path_to_utf8(const std::filesystem::path& path) {
    return u8string_to_string(path.generic_u8string());
}

inline std::string filename_to_utf8(const std::filesystem::path& path) {
    const auto name = path.filename();
    return name.empty() ? path_to_utf8(path) : path_to_utf8(name);
}

inline QString path_to_qstring(const std::filesystem::path& path) {
    const auto text = path_to_utf8(path);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline QString utf8_to_qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline std::filesystem::path path_from_qstring(const QString& text) {
#if defined(_WIN32)
    return std::filesystem::path(text.toStdWString());
#else
    const auto utf8 = text.toUtf8();
    return std::filesystem::path(std::string(utf8.constData(), static_cast<size_t>(utf8.size())));
#endif
}

inline std::filesystem::path path_from_utf8(std::string_view text) {
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const char ch : text) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
    }
    return std::filesystem::path(std::move(utf8));
}

} // namespace cristudio
