#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cristudio {

[[nodiscard]] std::string generic_path(const std::filesystem::path& path);
[[nodiscard]] std::string filename_of(const std::filesystem::path& path);
[[nodiscard]] std::string display_path_separators(std::string text);
[[nodiscard]] std::string archive_display_path(std::string_view text);
[[nodiscard]] std::string archive_leaf_name(std::string_view text);
[[nodiscard]] std::string lower_extension_text(std::string_view name);
[[nodiscard]] std::string bool_text(bool value);
[[nodiscard]] std::string number(uint64_t value);
[[nodiscard]] std::string byte_count(uint64_t value);
[[nodiscard]] std::string indexed_label(std::string_view label, uint64_t value);
[[nodiscard]] std::string float_text(float value);

void add_source_info(LoadedDocument& doc);
[[nodiscard]] LoadedDocument base_document(const std::filesystem::path& path, std::string format);

} // namespace cristudio
