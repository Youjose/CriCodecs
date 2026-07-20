#pragma once

#include "document/document_types.hpp"

#include "utf_table.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cristudio::modules::utf {

std::string column_type_name(cricodecs::utf::ColumnType type);
std::string column_flag_text(cricodecs::utf::ColumnFlag flags);
std::string guid_text(const cricodecs::utf::GUID& guid);
std::string data_probe_text(std::span<const uint8_t> bytes);
std::string value_text(const cricodecs::utf::UtfTable& utf, uint32_t row, uint32_t col);
std::string row_label(const cricodecs::utf::UtfTable& utf, uint32_t row);

std::optional<uint32_t> source_index(uint32_t row, uint32_t column_count, uint32_t col);
std::optional<std::pair<uint32_t, uint32_t>> row_col_from_source_index(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
);
std::expected<std::vector<uint8_t>, std::string> extract_cell_data(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
);

} // namespace cristudio::modules::utf
