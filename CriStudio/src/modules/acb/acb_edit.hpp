#pragma once

#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include "acb_container.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cristudio::modules::acb {

struct AssociatedAwbBytes {
    std::vector<uint8_t> bytes;
    std::optional<std::filesystem::path> source_path;
};

[[nodiscard]] QString associated_awb_default_name(
    const cricodecs::acb::AcbContainer& acb,
    QString title
);

[[nodiscard]] std::expected<AssociatedAwbBytes, QString> associated_awb_bytes(
    const cricodecs::acb::AcbContainer& acb
);

[[nodiscard]] std::expected<void, QString> validate_associated_awb(
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(
    const cricodecs::acb::AcbContainer& acb,
    const DecryptionKeys& keys
);

[[nodiscard]] std::expected<QString, QString> payload_preview(
    const cricodecs::acb::AcbContainer& acb,
    const DecryptionKeys& keys,
    int payload_kind,
    int index
);

[[nodiscard]] std::expected<cricodecs::utf::UtfTable, QString> payload_table(
    const cricodecs::acb::AcbContainer& acb,
    int payload_kind,
    int index
);

} // namespace cristudio::modules::acb
