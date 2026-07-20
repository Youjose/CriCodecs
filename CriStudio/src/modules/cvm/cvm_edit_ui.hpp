#pragma once

#include "modules/cvm/cvm_edit.hpp"

#include <QString>

#include <filesystem>
#include <optional>

class QWidget;
class QTableWidget;

namespace cristudio::modules::cvm {

[[nodiscard]] std::optional<std::filesystem::path> choose_entry_path(
    QWidget* parent,
    const cricodecs::cvm::CvmEntry& entry
);

[[nodiscard]] std::optional<MetadataOptions> choose_metadata_options(
    QWidget* parent,
    const cricodecs::cvm::CvmContainer& cvm
);

[[nodiscard]] std::optional<std::filesystem::path> choose_import_script(QWidget* parent);

[[nodiscard]] std::optional<std::filesystem::path> choose_export_script(
    QWidget* parent,
    QString title
);

void populate_editor_archive_table(QTableWidget* table, const cricodecs::cvm::CvmContainer& cvm);

} // namespace cristudio::modules::cvm
