#pragma once

#include "awb_container.hpp"
#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::awb {

struct ScratchArchive {
    cricodecs::awb::AwbContainer container;
    LoadedDocument document;
};

[[nodiscard]] ScratchArchive create_scratch_archive();

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(
    const cricodecs::awb::AwbContainer& awb,
    const DecryptionKeys& keys
);

[[nodiscard]] QString aac_probe_text(
    const cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    const DecryptionKeys& keys
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    const cricodecs::awb::AwbContainer& awb
);

[[nodiscard]] uint32_t add_file(
    cricodecs::awb::AwbContainer& awb,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> replace_file(
    cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> remove_file(
    cricodecs::awb::AwbContainer& awb,
    uint32_t index
);

[[nodiscard]] std::expected<void, std::string> move_file(
    cricodecs::awb::AwbContainer& awb,
    uint32_t from_index,
    uint32_t to_index
);

[[nodiscard]] std::expected<void, std::string> set_wave_id(
    cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    uint64_t wave_id
);

[[nodiscard]] std::expected<void, std::string> assign_wave_ids(
    cricodecs::awb::AwbContainer& awb,
    uint64_t start,
    uint64_t step
);

[[nodiscard]] std::expected<void, std::string> set_build_options(
    cricodecs::awb::AwbContainer& awb,
    uint8_t version,
    uint16_t alignment,
    uint16_t subkey,
    uint8_t id_size,
    uint8_t offset_size
);

} // namespace cristudio::modules::awb
