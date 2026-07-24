#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cristudio {

class OutputPathAllocator {
public:
    [[nodiscard]] std::filesystem::path allocate(const std::filesystem::path& requested);

private:
    [[nodiscard]] static std::string output_key(const std::filesystem::path& path);

    std::unordered_set<std::string> m_reserved;
    std::unordered_map<std::string, uint32_t> m_next_suffix;
};

[[nodiscard]] std::string safe_path_component(std::string_view raw);
[[nodiscard]] std::filesystem::path safe_relative_path(std::string_view raw_name);
[[nodiscard]] std::filesystem::path safe_document_name(const LoadedDocument& document);
[[nodiscard]] std::filesystem::path with_extension(std::filesystem::path path, std::string_view extension);
[[nodiscard]] std::filesystem::path without_extension(std::filesystem::path path);
[[nodiscard]] std::filesystem::path with_stem_suffix(
    const std::filesystem::path& base,
    std::string_view suffix,
    std::string_view extension
);

[[nodiscard]] std::expected<void, std::string> write_binary_file(
    const std::filesystem::path& output_path,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<void, std::string> write_text_file(
    const std::filesystem::path& output_path,
    std::string_view text,
    std::stop_token stop_token = {}
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_binary_file(
    const std::filesystem::path& path,
    std::stop_token stop_token = {}
);

} // namespace cristudio
