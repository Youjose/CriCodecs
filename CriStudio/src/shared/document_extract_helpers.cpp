#include "shared/document_extract_helpers.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace cristudio {

std::filesystem::path OutputPathAllocator::allocate(const std::filesystem::path& requested) {
    const auto requested_key = output_key(requested);
    if (m_reserved.insert(requested_key).second && !std::filesystem::exists(requested)) {
        return requested;
    }

    auto& next_suffix = m_next_suffix[requested_key];
    if (next_suffix == 0) {
        next_suffix = 1;
    }

    const auto parent = requested.parent_path();
    const auto stem = requested.stem();
    const auto extension = requested.extension();
    for (; next_suffix < 100000; ++next_suffix) {
        auto candidate = parent / (stem.string() + "_" + std::to_string(next_suffix) + extension.string());
        const auto candidate_key = output_key(candidate);
        if (m_reserved.insert(candidate_key).second && !std::filesystem::exists(candidate)) {
            ++next_suffix;
            return candidate;
        }
    }
    return requested;
}

std::string OutputPathAllocator::output_key(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

std::string safe_path_component(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const unsigned char ch : raw) {
        if (ch < 0x20 || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            out.push_back('_');
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty() || out == "." || out == "..") {
        return "_";
    }
    return out;
}

std::filesystem::path safe_relative_path(std::string_view raw_name) {
    std::filesystem::path path;
    const auto normalized = archive_display_path(raw_name);
    size_t start = 0;
    while (start < normalized.size()) {
        const auto separator = normalized.find('/', start);
        const auto end = separator == std::string::npos ? normalized.size() : separator;
        if (end > start) {
            path /= safe_path_component(std::string_view(normalized).substr(start, end - start));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    if (path.empty()) {
        path = "_";
    }
    return path;
}

std::filesystem::path safe_document_name(const LoadedDocument& document) {
    auto name = document.display_name.empty() ? filename_of(document.path) : document.display_name;
    if (name.empty()) {
        name = "document";
    }
    return safe_relative_path(archive_leaf_name(name));
}

std::filesystem::path with_extension(std::filesystem::path path, std::string_view extension) {
    if (!extension.empty()) {
        path.replace_extension(std::filesystem::path(std::string(extension)));
    }
    return path;
}

std::filesystem::path without_extension(std::filesystem::path path) {
    path.replace_extension();
    return path;
}

std::filesystem::path with_stem_suffix(
    const std::filesystem::path& base,
    std::string_view suffix,
    std::string_view extension
) {
    auto result = base.parent_path() / (base.stem().string() + std::string(suffix));
    result.replace_extension(std::filesystem::path(std::string(extension)));
    return result;
}

std::expected<void, std::string> write_binary_file(
    const std::filesystem::path& output_path,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected("extraction canceled");
    }

    std::error_code filesystem_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("could not create output directory: " + filesystem_error.message());
        }
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return std::unexpected("could not open output file: " + output_path.string());
    }
    constexpr size_t write_chunk_size = 4u * 1024u * 1024u;
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            output.close();
            std::filesystem::remove(output_path, filesystem_error);
            return std::unexpected("extraction canceled");
        }
        const auto size = std::min(write_chunk_size, bytes.size() - offset);
        output.write(
            reinterpret_cast<const char*>(bytes.data() + offset),
            static_cast<std::streamsize>(size)
        );
        if (!output) {
            return std::unexpected("could not write output file: " + output_path.string());
        }
        offset += size;
    }
    return {};
}

std::expected<void, std::string> write_text_file(
    const std::filesystem::path& output_path,
    std::string_view text,
    std::stop_token stop_token
) {
    return write_binary_file(
        output_path,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()
        ),
        stop_token
    );
}

std::expected<std::vector<uint8_t>, std::string> read_binary_file(
    const std::filesystem::path& path,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected("extraction canceled");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("could not open input file: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) {
        return std::unexpected("could not size input file: " + path.string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    constexpr size_t read_chunk_size = 4u * 1024u * 1024u;
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (stop_token.stop_requested()) {
            return std::unexpected("extraction canceled");
        }
        const auto chunk_size = std::min(read_chunk_size, bytes.size() - offset);
        input.read(
            reinterpret_cast<char*>(bytes.data() + offset),
            static_cast<std::streamsize>(chunk_size)
        );
        if (!input) {
            return std::unexpected("could not read input file: " + path.string());
        }
        offset += chunk_size;
    }
    return bytes;
}

} // namespace cristudio
