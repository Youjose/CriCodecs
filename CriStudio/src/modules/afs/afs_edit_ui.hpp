#pragma once

#include "afs_container.hpp"

#include <QString>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

class QWidget;
class QTableWidget;

namespace cristudio::modules::afs {

struct AddFileOptions {
    uint32_t file_id = 0;
    std::optional<std::string> name;
    std::optional<std::string> header_source_name;
    std::array<uint8_t, 12> directory_metadata{};
};

struct BuildOptions {
    uint32_t alignment = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT;
    bool directory_table_enabled = true;
    std::optional<uint32_t> first_payload_offset;
};

struct AlsImportOptions {
    std::filesystem::path file_list_path;
    uint32_t alignment = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT;
    bool directory_table_enabled = true;
    std::optional<std::filesystem::path> source_root;
};

[[nodiscard]] std::optional<AddFileOptions> choose_add_file_options(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs,
    const QString& file_path
);

[[nodiscard]] std::optional<uint32_t> choose_reserve_file_id(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs
);

[[nodiscard]] std::optional<std::optional<cricodecs::afs::AfsDirectoryTimestamp>> choose_directory_timestamp(
    QWidget* parent,
    const cricodecs::afs::AfsEntry& entry,
    uint32_t file_id
);

[[nodiscard]] std::optional<BuildOptions> choose_build_options(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs
);

[[nodiscard]] std::optional<AlsImportOptions> choose_als_import_options(
    QWidget* parent,
    const cricodecs::afs::AfsContainer* current_afs
);

[[nodiscard]] std::expected<std::optional<std::filesystem::path>, QString> export_file_id_header(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs,
    QString default_archive_name
);

void populate_editor_archive_table(QTableWidget* table, const cricodecs::afs::AfsContainer& afs);

} // namespace cristudio::modules::afs
