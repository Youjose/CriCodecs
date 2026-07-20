#pragma once

#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include "adx_codec.hpp"

#include <QString>

#include <expected>
#include <optional>
#include <vector>

class QWidget;

namespace cristudio::modules::adx {

[[nodiscard]] std::expected<std::optional<cricodecs::adx::AdxEncodeConfig>, QString> choose_rebuild_config(
    QWidget* parent,
    const cricodecs::adx::Adx& adx,
    const DecryptionKeys& keys
);

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::adx::Adx& adx);

} // namespace cristudio::modules::adx
