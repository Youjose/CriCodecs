/**
 * @file text_encoding.cpp
 * @brief Legacy CRI string encoding conversion.
 *
 * Project-local implementation for preserving raw UTF metadata bytes while
 * decoding known CRI-era text encodings at API boundaries. Implemented by
 * Youjose.
 */

#include "text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>
#include <version>

#if defined(__cpp_lib_text_encoding) && __cpp_lib_text_encoding >= 202306L
#  define CRICODECS_HAS_STD_TEXT_ENCODING 1
#  include <text_encoding>
#endif

#if defined(__has_include)
#  if __has_include(<iconv.h>)
#    define CRICODECS_HAS_ICONV 1
#    include <iconv.h>
#  endif
#endif

#if defined(__unix__) || defined(__APPLE__)
#  include <langinfo.h>
#endif

namespace cricodecs::text {
namespace {

std::string normalize_encoding(std::string_view encoding) {
    std::string normalized;
    normalized.reserve(encoding.size());
    for (const unsigned char ch : encoding) {
        if (ch == '_' || ch == '-') {
            normalized.push_back('-');
        } else {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}

bool same_encoding(std::string_view lhs, std::string_view rhs) {
#if defined(CRICODECS_HAS_STD_TEXT_ENCODING)
    const std::text_encoding lhs_encoding(lhs);
    const std::text_encoding rhs_encoding(rhs);
    if (lhs_encoding.mib() != std::text_encoding::id::unknown &&
        rhs_encoding.mib() != std::text_encoding::id::unknown) {
        return lhs_encoding == rhs_encoding;
    }
#endif
    return normalize_encoding(lhs) == normalize_encoding(rhs);
}

std::string converter_encoding_name(std::string_view encoding) {
#if defined(CRICODECS_HAS_STD_TEXT_ENCODING)
    const std::text_encoding known(encoding);
    if (known.mib() != std::text_encoding::id::unknown) {
        if (const char* name = known.name(); name != nullptr && name[0] != '\0') {
            return name;
        }
    }
#endif
    return std::string(encoding);
}

bool is_utf16_name(std::string_view encoding) {
    const auto normalized = normalize_encoding(encoding);
    return normalized == "UTF-16" ||
        normalized == "UTF-16LE" ||
        normalized == "UTF-16BE";
}

bool is_ascii_only(std::span<const uint8_t> input) {
    return std::ranges::all_of(input, [](uint8_t byte) {
        return byte <= 0x7Fu;
    });
}

bool is_ascii_compatible_encoding(std::string_view encoding) {
    const auto normalized = normalize_encoding(encoding);
    return normalized == "UTF-8" ||
        normalized == "US-ASCII" ||
        normalized == "ASCII" ||
        normalized == "ANSI-X3.4-1968" ||
        normalized == "GBK" ||
        normalized == "CP936" ||
        normalized == "CP932" ||
        normalized == "SHIFT-JIS" ||
        normalized == "SJIS";
}

bool can_decode_ascii_directly(const EncodingOptions& options) {
    if (!is_auto_encoding(options)) {
        return is_ascii_compatible_encoding(*options.encoding);
    }
    return is_ascii_compatible_encoding(system_encoding());
}

std::vector<std::string> candidate_encodings(const EncodingOptions& options) {
    if (!is_auto_encoding(options)) {
        return {*options.encoding};
    }

    std::vector<std::string> candidates;
    const auto add_unique = [&](std::string_view name) {
        if (name.empty()) {
            return;
        }
        const auto exists = std::ranges::any_of(candidates, [&](const std::string& existing) {
            return same_encoding(existing, name);
        });
        if (!exists) {
            candidates.emplace_back(converter_encoding_name(name));
        }
    };

    add_unique(system_encoding());
    add_unique("UTF-8");
    add_unique("GBK");
    add_unique("CP936");
    add_unique("CP932");
    add_unique("Shift-JIS");
    add_unique("SJIS");
    return candidates;
}

#if defined(CRICODECS_HAS_ICONV)
std::expected<std::string, std::string> iconv_convert(
    std::span<const uint8_t> input,
    std::string_view from_encoding,
    std::string_view to_encoding
) {
    iconv_t cd = iconv_open(std::string(to_encoding).c_str(), std::string(from_encoding).c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return std::unexpected("iconv does not support conversion from " + std::string(from_encoding) +
            " to " + std::string(to_encoding));
    }

    std::string output;
    output.resize(std::max<size_t>(input.size() * 4u, 16u));

    char* in_ptr = const_cast<char*>(reinterpret_cast<const char*>(input.data()));
    size_t in_left = input.size();
    char* out_ptr = output.data();
    size_t out_left = output.size();

    while (true) {
        const size_t result = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        if (result != static_cast<size_t>(-1)) {
            break;
        }
        if (errno == E2BIG) {
            const size_t used = output.size() - out_left;
            output.resize(output.size() * 2u);
            out_ptr = output.data() + used;
            out_left = output.size() - used;
            continue;
        }

        const std::string error = std::strerror(errno);
        iconv_close(cd);
        return std::unexpected("text conversion failed from " + std::string(from_encoding) +
            " to " + std::string(to_encoding) + ": " + error);
    }

    output.resize(output.size() - out_left);
    iconv_close(cd);
    return output;
}
#else
bool valid_utf8(std::span<const uint8_t> input) {
    size_t index = 0;
    while (index < input.size()) {
        const uint8_t lead = input[index++];
        if (lead <= 0x7F) {
            continue;
        }

        size_t continuation_count = 0;
        if ((lead & 0xE0u) == 0xC0u) {
            continuation_count = 1;
        } else if ((lead & 0xF0u) == 0xE0u) {
            continuation_count = 2;
        } else if ((lead & 0xF8u) == 0xF0u) {
            continuation_count = 3;
        } else {
            return false;
        }

        if (index + continuation_count > input.size()) {
            return false;
        }
        for (size_t i = 0; i < continuation_count; ++i) {
            if ((input[index++] & 0xC0u) != 0x80u) {
                return false;
            }
        }
    }
    return true;
}

std::expected<std::string, std::string> iconv_convert(
    std::span<const uint8_t> input,
    std::string_view from_encoding,
    std::string_view to_encoding
) {
    if (same_encoding(from_encoding, "UTF-8") && same_encoding(to_encoding, "UTF-8") && valid_utf8(input)) {
        return std::string(reinterpret_cast<const char*>(input.data()), input.size());
    }
    return std::unexpected("text conversion requires iconv for encoding " + std::string(from_encoding));
}
#endif

} // namespace

bool is_auto_encoding(const EncodingOptions& options) noexcept {
    return !options.encoding.has_value() || same_encoding(*options.encoding, "auto");
}

std::string system_encoding() {
#if defined(__unix__) || defined(__APPLE__)
    if (const char* codeset = nl_langinfo(CODESET); codeset != nullptr && codeset[0] != '\0') {
        return codeset;
    }
#endif
    return "UTF-8";
}

std::expected<std::string, std::string> decode_to_utf8(
    std::span<const uint8_t> bytes,
    const EncodingOptions& options
) {
    if (bytes.empty()) {
        return std::string{};
    }

    if (is_ascii_only(bytes) && can_decode_ascii_directly(options)) {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    std::string last_error;
    for (const auto& encoding : candidate_encodings(options)) {
        auto converted = iconv_convert(bytes, encoding, "UTF-8");
        if (converted) {
            return *converted;
        }
        last_error = converted.error();
    }
    return std::unexpected(last_error.empty() ? "text decode failed" : last_error);
}

std::expected<std::vector<uint8_t>, std::string> encode_from_utf8(
    std::string_view text,
    const EncodingOptions& options
) {
    const auto input = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text.data()),
        text.size()
    );

    std::string last_error;
    for (const auto& encoding : candidate_encodings(options)) {
        auto converted = iconv_convert(input, "UTF-8", encoding);
        if (converted) {
            return std::vector<uint8_t>(converted->begin(), converted->end());
        }
        last_error = converted.error();
    }
    return std::unexpected(last_error.empty() ? "text encode failed" : last_error);
}

std::expected<std::vector<uint8_t>, std::string> encode_cri_string(
    std::string_view text,
    const EncodingOptions& options
) {
    auto encoded = encode_from_utf8(text, options);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    if (contains_nul(*encoded)) {
        return std::unexpected("CRI UTF string encoding produced embedded NUL bytes");
    }
    if (!is_auto_encoding(options) && is_utf16_name(*options.encoding)) {
        return std::unexpected("CRI UTF string-table storage does not support UTF-16 text");
    }
    return encoded;
}

bool contains_nul(std::span<const uint8_t> bytes) noexcept {
    return std::ranges::find(bytes, uint8_t{0}) != bytes.end();
}

bool contains_nul(std::string_view bytes) noexcept {
    return bytes.find('\0') != std::string_view::npos;
}

} // namespace cricodecs::text
