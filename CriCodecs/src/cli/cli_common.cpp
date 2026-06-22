#include "cli_internal.hpp"

#ifndef CRICODECS_VERSION
#define CRICODECS_VERSION "unknown"
#endif

#ifndef CRICODECS_GIT_HASH
#define CRICODECS_GIT_HASH "unknown"
#endif

namespace cricodecs::cli::detail {

[[nodiscard]] std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

[[nodiscard]] std::string trim_ascii(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] std::expected<uint64_t, std::string> parse_u64(std::string_view text, std::string_view context) {
    const std::string trimmed = trim_ascii(text);
    if (trimmed.empty()) {
        return std::unexpected(std::string(context) + " requires a value");
    }

    int base = 10;
    size_t offset = 0;
    if (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        base = 16;
        offset = 2;
    }

    uint64_t value = 0;
    const auto* first = trimmed.data() + static_cast<std::ptrdiff_t>(offset);
    const auto* last = trimmed.data() + static_cast<std::ptrdiff_t>(trimmed.size());
    if (first == last) {
        return std::unexpected(std::string(context) + " requires a numeric value");
    }

    const auto [ptr, error] = std::from_chars(first, last, value, base);
    if (error != std::errc{} || ptr != last) {
        return std::unexpected(std::string(context) + " requires an unsigned integer");
    }
    return value;
}

[[nodiscard]] std::expected<uint16_t, std::string> parse_u16(std::string_view text, std::string_view context) {
    auto parsed = parse_u64(text, context);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (*parsed > std::numeric_limits<uint16_t>::max()) {
        return std::unexpected(std::string(context) + " is out of range for uint16");
    }
    return static_cast<uint16_t>(*parsed);
}

[[nodiscard]] std::optional<size_t> parse_index_target(std::string_view text) {
    const std::string trimmed = trim_ascii(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    size_t value = 0;
    const auto [ptr, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value, 10);
    if (error != std::errc{} || ptr != trimmed.data() + trimmed.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::expected<std::pair<std::string, std::string>, std::string> parse_pair_value(
    std::string_view text,
    std::string_view option
) {
    const size_t split = text.find('=');
    if (split == std::string_view::npos || split == 0 || split + 1 == text.size()) {
        return std::unexpected(std::string(option) + " expects LEFT=RIGHT");
    }
    return std::pair{
        trim_ascii(text.substr(0, split)),
        trim_ascii(text.substr(split + 1)),
    };
}

[[nodiscard]] bool contains_placeholder(std::string_view text) {
    return text.find("?i") != std::string_view::npos ||
           text.find("?e") != std::string_view::npos ||
           text.find("?s") != std::string_view::npos;
}

void replace_all(std::string& text, std::string_view needle, std::string_view replacement) {
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

[[nodiscard]] std::string escape_json(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    std::ostringstream stream;
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                    escaped += stream.str();
                } else {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string quote_json(std::string_view text) {
    return "\"" + escape_json(text) + "\"";
}

template <typename Range, typename Fn>
void join_json_array(std::ostream& out, const Range& range, Fn&& fn) {
    out << '[';
    bool first = true;
    for (const auto& item : range) {
        if (!first) {
            out << ',';
        }
        first = false;
        fn(item);
    }
    out << ']';
}

[[nodiscard]] std::string bool_text(bool value) {
    return value ? "true" : "false";
}

template <typename T>
[[nodiscard]] std::string decimal_text(T value) {
    return std::to_string(value);
}

[[nodiscard]] std::string hex_text(uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << value;
    return stream.str();
}

[[nodiscard]] std::string build_identity() {
    return std::string(CRICODECS_VERSION) + " (" + CRICODECS_GIT_HASH + ")";
}

[[nodiscard]] uint32_t be32(std::span<const uint8_t> bytes, size_t offset) {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return (static_cast<uint32_t>(bytes[offset]) << 24u) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8u) |
           static_cast<uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] bool has_magic_at(std::span<const uint8_t> bytes, size_t offset, std::string_view magic) {
    return bytes.size() >= offset + magic.size() &&
           std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] bool has_cvm_header(std::span<const uint8_t> bytes) {
    return has_magic_at(bytes, 0, "CVMH") && has_magic_at(bytes, 0x800, "ZONE");
}

[[nodiscard]] bool looks_like_acx(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() < 8 || bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0) {
        return false;
    }
    const uint32_t entry_count =
        (static_cast<uint32_t>(bytes[4]) << 24u) |
        (static_cast<uint32_t>(bytes[5]) << 16u) |
        (static_cast<uint32_t>(bytes[6]) << 8u) |
        static_cast<uint32_t>(bytes[7]);
    return entry_count > 0 && entry_count <= 0x10000u;
}

void push_unique(std::vector<Format>& formats, Format format) {
    if (!std::ranges::contains(formats, format)) {
        formats.push_back(format);
    }
}

[[nodiscard]] text::EncodingOptions encoding_options(const Options& options) {
    if (!options.encoding.has_value()) {
        return {};
    }
    text::EncodingOptions value;
    value.encoding = options.encoding;
    return value;
}

[[nodiscard]] std::filesystem::path default_output_path(
    const std::filesystem::path& input,
    Format format,
    bool raw
) {
    const auto parent = input.parent_path();
    auto stem = input.stem().empty() ? input.filename() : input.stem();
    if (format == Format::utf) {
        return {};
    }

    if (raw) {
        switch (format) {
            case Format::adx:
            case Format::ahx: {
                auto output_stem = stem;
                output_stem += "_raw";
                return parent / output_stem.replace_extension(format == Format::ahx ? ".ahx" : ".adx");
            }
            case Format::hca:
                stem += "_raw";
                return parent / stem.replace_extension(".hca");
            case Format::aax:
                stem += "_raw";
                return parent / stem.replace_extension(".adx");
            default:
                return parent / stem;
        }
    }

    switch (format) {
        case Format::adx:
        case Format::ahx:
        case Format::aax:
        case Format::hca:
            return parent / stem.replace_extension(".wav");
        default:
            return parent / stem;
    }
}

[[nodiscard]] std::filesystem::path crypto_output_path(
    const std::filesystem::path& input,
    std::string_view suffix
) {
    const auto parent = input.parent_path();
    auto output_name = input.stem().empty() ? input.filename() : input.stem();
    output_name += suffix;
    return parent / output_name.replace_extension(input.extension());
}

[[nodiscard]] std::expected<void, std::string> write_bytes_file(
    const std::filesystem::path& output_path,
    std::span<const uint8_t> bytes
) {
    std::error_code filesystem_error;
    if (const auto parent = output_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("could not create output directory: " + filesystem_error.message());
        }
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return std::unexpected("could not open output file: " + output_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        return std::unexpected("could not write output file: " + output_path.string());
    }
    return {};
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_bytes_file(
    const std::filesystem::path& input_path
) {
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        return std::unexpected("could not open input file: " + input_path.string());
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("could not determine input file size: " + input_path.string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return std::unexpected("could not read input file: " + input_path.string());
        }
    }
    return bytes;
}

[[nodiscard]] std::expected<std::vector<std::pair<std::filesystem::path, std::filesystem::path>>, std::string>
collect_directory_files(const std::filesystem::path& input_dir) {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(input_dir, filesystem_error) ||
        !std::filesystem::is_directory(input_dir, filesystem_error)) {
        return std::unexpected("input path is not a directory: " + input_dir.string());
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
    for (std::filesystem::recursive_directory_iterator it(input_dir, filesystem_error), end; it != end; it.increment(filesystem_error)) {
        if (filesystem_error) {
            return std::unexpected("directory walk failed: " + filesystem_error.message());
        }
        if (!it->is_regular_file()) {
            continue;
        }
        auto relative = std::filesystem::relative(it->path(), input_dir, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("could not compute relative path for " + it->path().string());
        }
        files.emplace_back(it->path(), relative.generic_string());
    }
    std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second.generic_string() < rhs.second.generic_string();
    });
    if (files.empty()) {
        return std::unexpected("input directory contains no files");
    }
    return files;
}

[[nodiscard]] std::expected<std::array<uint16_t, 3>, std::string> parse_key_triplet(std::string_view text) {
    std::array<uint16_t, 3> values{};
    size_t next = 0;
    for (size_t index = 0; index < values.size(); ++index) {
        const size_t split = text.find_first_of(",:", next);
        const std::string_view part = split == std::string_view::npos
            ? text.substr(next)
            : text.substr(next, split - next);
        auto parsed = parse_u16(part, "--key");
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        values[index] = *parsed;
        if (split == std::string_view::npos) {
            if (index + 1 != values.size()) {
                return std::unexpected("`--key` triplet requires exactly three values");
            }
            next = text.size();
            continue;
        }
        next = split + 1;
    }
    if (next < text.size() && text.find_first_not_of(" \t\r\n", next) != std::string_view::npos) {
        return std::unexpected("`--key` triplet requires exactly three values");
    }
    return values;
}

[[nodiscard]] std::expected<void, std::string> apply_adx_key(adx::Adx& audio, const Options& options) {
    if (!options.key.has_value()) {
        return {};
    }

    if (options.key->find_first_of(",:") != std::string::npos) {
        auto triplet = parse_key_triplet(*options.key);
        if (!triplet) {
            return std::unexpected(triplet.error());
        }
        if (audio.is_ahx()) {
            audio.set_ahx_key((*triplet)[0], (*triplet)[1], (*triplet)[2]);
        } else {
            audio.set_key_triplet((*triplet)[0], (*triplet)[1], (*triplet)[2]);
        }
        return {};
    }

    if (auto numeric = parse_u64(*options.key, "--key"); numeric) {
        if (audio.is_ahx()) {
            return std::unexpected("AHX key must be a start,mult,add triplet");
        }
        audio.set_key_type9(*numeric, options.subkey.value_or(0));
        return {};
    }

    if (audio.is_ahx()) {
        return std::unexpected("AHX key must be a numeric keycode or a start,mult,add triplet");
    }

    audio.set_key_type8(*options.key);
    return {};
}

[[nodiscard]] std::expected<uint64_t, std::string> hca_keycode(const Options& options) {
    if (!options.key.has_value()) {
        return uint64_t{0};
    }
    return parse_u64(*options.key, "--key");
}


} // namespace cricodecs::cli::detail
