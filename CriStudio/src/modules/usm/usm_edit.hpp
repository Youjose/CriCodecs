#pragma once

#include "modules/transform_detail.hpp"

#include "usm_container.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace cristudio::modules::usm {

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(cricodecs::usm::UsmReader& usm);
[[nodiscard]] std::vector<TransformDetailRow> chunk_detail_rows(const cricodecs::usm::UsmReader& usm);
[[nodiscard]] TransformDetailRow chunk_detail_row(const cricodecs::usm::UsmReader& usm, size_t index);
[[nodiscard]] TransformDetailRow chunk_detail_row(const cricodecs::usm::UsmReader& usm, size_t index, uint64_t file_offset);
[[nodiscard]] std::expected<QString, QString> chunk_payload_preview(
    const cricodecs::usm::UsmReader& usm,
    int index
);
[[nodiscard]] std::expected<std::vector<uint8_t>, QString> chunk_payload_sample(
    const cricodecs::usm::UsmReader& usm,
    int index,
    size_t max_bytes = 4096
);
[[nodiscard]] std::expected<QString, QString> utf_payload_preview(
    cricodecs::usm::UsmReader& usm,
    int index
);
[[nodiscard]] std::expected<cricodecs::utf::UtfTable, QString> utf_payload_table(
    cricodecs::usm::UsmReader& usm,
    int index
);
[[nodiscard]] std::expected<QString, QString> stream_payload_preview(
    cricodecs::usm::UsmReader& usm,
    int index
);
[[nodiscard]] std::expected<std::vector<uint8_t>, QString> stream_payload_sample(
    cricodecs::usm::UsmReader& usm,
    int index,
    size_t max_bytes = 4096
);

} // namespace cristudio::modules::usm
