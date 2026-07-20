#pragma once

#include "afs_container.hpp"
#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::afs {

struct ScratchArchive {
    cricodecs::afs::AfsContainer container;
    LoadedDocument document;
};

[[nodiscard]] ScratchArchive create_scratch_archive();

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::afs::AfsContainer& afs);

[[nodiscard]] std::expected<void, std::string> add_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t file_id,
    std::span<const uint8_t> bytes,
    std::optional<std::string> name,
    std::array<uint8_t, 12> directory_metadata,
    std::optional<std::string> header_source_name
);

[[nodiscard]] std::expected<void, std::string> replace_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> remove_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index
);

[[nodiscard]] std::expected<void, std::string> move_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t from_index,
    uint32_t to_index
);

[[nodiscard]] std::expected<void, std::string> rename_file(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<std::string> name
);

[[nodiscard]] std::expected<void, std::string> set_header_source_name(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<std::string> name
);

[[nodiscard]] std::expected<void, std::string> set_directory_metadata(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::array<uint8_t, 12> metadata
);

void reserve_file_id(
    cricodecs::afs::AfsContainer& afs,
    uint32_t file_id
);

[[nodiscard]] std::expected<void, std::string> set_directory_timestamp(
    cricodecs::afs::AfsContainer& afs,
    uint32_t index,
    std::optional<cricodecs::afs::AfsDirectoryTimestamp> timestamp
);

[[nodiscard]] std::expected<void, std::string> set_build_options(
    cricodecs::afs::AfsContainer& afs,
    uint32_t alignment,
    bool directory_table_enabled,
    std::optional<uint32_t> first_payload_offset
);

[[nodiscard]] std::expected<cricodecs::afs::AfsContainer, std::string> import_als(
    const std::filesystem::path& file_list_path,
    uint32_t alignment,
    bool directory_table_enabled,
    const std::optional<std::filesystem::path>& source_root
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    cricodecs::afs::AfsContainer& afs
);

} // namespace cristudio::modules::afs
