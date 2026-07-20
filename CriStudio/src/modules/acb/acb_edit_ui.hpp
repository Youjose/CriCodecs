#pragma once

#include "acb_container.hpp"
#include "document/document_types.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

class QWidget;

namespace cristudio::modules::acb {

struct AssociatedAwbOpenPayload {
    std::string display_name;
    DecryptionKeys keys;
    std::filesystem::path source_path;
    std::filesystem::path source_archive_path;
    std::vector<uint8_t> bytes;
};

struct AssociatedAwbExportPayload {
    std::filesystem::path output_path;
    std::vector<uint8_t> bytes;
};

[[nodiscard]] std::expected<AssociatedAwbOpenPayload, QString> prepare_associated_awb_open(
    QWidget* parent,
    const cricodecs::acb::AcbContainer& acb,
    QString title,
    DecryptionKeys keys,
    const std::filesystem::path& source_archive_path
);

[[nodiscard]] std::expected<std::optional<AssociatedAwbExportPayload>, QString> choose_associated_awb_export(
    QWidget* parent,
    const cricodecs::acb::AcbContainer& acb,
    QString title
);

} // namespace cristudio::modules::acb
