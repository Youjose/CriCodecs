#pragma once

#include "document/document_types.hpp"
#include "utf_table.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace cristudio::modules::utf {

struct ScratchTableSession {
    cricodecs::utf::UtfTable table;
    std::vector<uint8_t> bytes;
    LoadedDocument document;
};

[[nodiscard]] ScratchTableSession create_scratch_table_session();

[[nodiscard]] std::expected<void, std::string> rename_table(
    cricodecs::utf::UtfTable& utf,
    std::string name
);

[[nodiscard]] std::expected<void, std::string> set_value(
    cricodecs::utf::UtfTable& utf,
    uint32_t row,
    uint32_t col,
    cricodecs::utf::Value value
);

[[nodiscard]] std::expected<void, std::string> rename_column(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    std::string name
);

[[nodiscard]] std::expected<void, std::string> set_column_type(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::ColumnType type
);

[[nodiscard]] std::expected<void, std::string> set_column_flag(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::ColumnFlag flag
);

[[nodiscard]] std::expected<void, std::string> set_default_value(
    cricodecs::utf::UtfTable& utf,
    uint32_t col,
    cricodecs::utf::Value value
);

[[nodiscard]] uint32_t add_row(cricodecs::utf::UtfTable& utf);

[[nodiscard]] std::expected<void, std::string> remove_row(
    cricodecs::utf::UtfTable& utf,
    uint32_t row
);

void add_column(
    cricodecs::utf::UtfTable& utf,
    std::string name,
    cricodecs::utf::ColumnType type
);

[[nodiscard]] std::expected<void, std::string> remove_column(
    cricodecs::utf::UtfTable& utf,
    uint32_t col
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> cell_bytes(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
);

[[nodiscard]] std::vector<uint8_t> build_session_bytes(const cricodecs::utf::UtfTable& utf);

} // namespace cristudio::modules::utf
