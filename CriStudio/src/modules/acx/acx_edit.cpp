#include "modules/acx/acx_edit.hpp"

#include <optional>

namespace cristudio::modules::acx {
namespace {

template <typename T>
QString optional_number(const std::optional<T>& value) {
    return value ? QString::number(static_cast<qulonglong>(*value)) : QStringLiteral("-");
}

} // namespace

ScratchArchive create_scratch_archive() {
    return ScratchArchive{
        .container = cricodecs::acx::AcxContainer{},
        .document = LoadedDocument{
            .display_name = "NewArchive.acx",
            .format = "ACX archive (scratch)",
            .file_size = 0,
            .info = {
                {"Source", "Scratch ACX archive"},
                {"Entries", "0"}
            },
            .entry_columns = {"Index", "Type", "Offset", "Size", "Suggested Path"},
            .entry_column_types = {"integer", "type", "offset", "size", "path"},
            .entries = {}
        }
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::acx::AcxContainer& acx) {
    return {
        {QStringLiteral("Entries"), QString::number(acx.entry_count())},
        {QStringLiteral("Table size"), QString::number(acx.table_size())},
        {QStringLiteral("First payload offset"), optional_number(acx.first_payload_offset())},
        {QStringLiteral("Payload end offset"), optional_number(acx.payload_end_offset())},
        {QStringLiteral("ADX entries"), QString::number(acx.type_count(cricodecs::acx::AcxEntryType::adx))},
        {QStringLiteral("Ogg entries"), QString::number(acx.type_count(cricodecs::acx::AcxEntryType::ogg))},
        {QStringLiteral("Unknown entries"), QString::number(acx.type_count(cricodecs::acx::AcxEntryType::unknown))}
    };
}

std::expected<void, std::string> add_file(
    cricodecs::acx::AcxContainer& acx,
    std::span<const uint8_t> bytes
) {
    return acx.add_file(bytes);
}

std::expected<void, std::string> replace_file(
    cricodecs::acx::AcxContainer& acx,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    return acx.set_file_data(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::acx::AcxContainer& acx, uint32_t index) {
    return acx.remove_file(index);
}

std::expected<void, std::string> move_file(
    cricodecs::acx::AcxContainer& acx,
    uint32_t from_index,
    uint32_t to_index
) {
    return acx.move_file(from_index, to_index);
}

std::filesystem::path suggested_path(const cricodecs::acx::AcxContainer& acx, uint32_t index) {
    return acx.entry(index).suggested_path();
}

std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(const cricodecs::acx::AcxContainer& acx) {
    return acx.rebuild();
}

} // namespace cristudio::modules::acx
