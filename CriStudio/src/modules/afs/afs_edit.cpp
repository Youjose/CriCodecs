#include "modules/afs/afs_edit.hpp"

#include <utility>

namespace cristudio::modules::afs {
namespace {

QString optional_number(const std::optional<uint32_t>& value) {
    return value ? QString::number(*value) : QStringLiteral("-");
}

} // namespace

ScratchArchive create_scratch_archive() {
    auto afs = cricodecs::afs::AfsContainer::create(cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT, true);
    return ScratchArchive{
        .container = std::move(afs),
        .document = LoadedDocument{
            .display_name = "NewArchive.afs",
            .format = "AFS archive (scratch)",
            .file_size = 0,
            .info = {
                {"Source", "Scratch AFS archive"},
                {"Slots", "0"},
                {"Alignment", std::to_string(cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT)}
            },
            .entry_columns = {"ID", "Present", "Name", "Type", "Offset", "Size"},
            .entry_column_types = {"integer", "state", "string", "type", "offset", "size"},
            .entries = {}
        }
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::afs::AfsContainer& afs) {
    return {
        {QStringLiteral("Slots"), QString::number(afs.entry_count())},
        {QStringLiteral("Present entries"), QString::number(afs.present_entry_count())},
        {QStringLiteral("Alignment"), QString::number(afs.alignment())},
        {QStringLiteral("Directory table in source"), afs.has_directory_table() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Directory table on build"), afs.directory_table_enabled() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Directory offset"), optional_number(afs.directory_table_offset())},
        {QStringLiteral("Directory size"), optional_number(afs.directory_table_size())},
        {QStringLiteral("First payload offset"), optional_number(afs.first_payload_offset())}
    };
}

std::expected<void, std::string> add_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t file_id,
    std::span<const uint8_t> bytes,
    std::optional<std::string> name,
    std::array<uint8_t, 12> directory_metadata,
    std::optional<std::string> header_source_name
) {
    afs.add_file_at_id(file_id, bytes, std::move(name), directory_metadata);
    return afs.set_header_source_name(file_id, std::move(header_source_name));
}

std::expected<void, std::string> replace_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    return afs.replace_file(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::afs::AfsContainer& afs, uint32_t index) {
    return afs.remove_file(index);
}

std::expected<void, std::string> move_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t from_index,
    uint32_t to_index
) {
    return afs.move_file(from_index, to_index);
}

std::expected<void, std::string> rename_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<std::string> name
) {
    return afs.set_name(index, std::move(name));
}

std::expected<void, std::string> set_header_source_name(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<std::string> name
) {
    return afs.set_header_source_name(index, std::move(name));
}

std::expected<void, std::string> set_directory_metadata(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::array<uint8_t, 12> metadata
) {
    return afs.set_directory_metadata(index, metadata);
}

void reserve_file_id(cricodecs::afs::AfsContainer& afs, uint32_t file_id) {
    afs.reserve_file_id(file_id);
}

std::expected<void, std::string> set_directory_timestamp(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<cricodecs::afs::AfsDirectoryTimestamp> timestamp
) {
    return afs.set_directory_timestamp(index, timestamp);
}

std::expected<void, std::string> set_build_options(
    cricodecs::afs::AfsContainer& afs,
    uint32_t alignment,
    bool directory_table_enabled,
    std::optional<uint32_t> first_payload_offset
) {
    auto result = afs.set_alignment(alignment);
    if (result) {
        result = afs.set_first_payload_offset(first_payload_offset);
    }
    if (!result) {
        return result;
    }
    afs.set_directory_table_enabled(directory_table_enabled);
    return {};
}

std::expected<cricodecs::afs::AfsContainer, std::string> import_als(
    const std::filesystem::path& file_list_path,
    uint32_t alignment,
    bool directory_table_enabled,
    const std::optional<std::filesystem::path>& source_root
) {
    return cricodecs::afs::AfsContainer::create_from_als(
        file_list_path,
        alignment,
        directory_table_enabled,
        source_root
    );
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(cricodecs::afs::AfsContainer& afs) {
    return afs.build();
}

} // namespace cristudio::modules::afs
