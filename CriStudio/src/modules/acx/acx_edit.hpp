#pragma once

#include "acx_container.hpp"
#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::acx {

struct ScratchArchive {
    cricodecs::acx::AcxContainer container;
    LoadedDocument document;
};

[[nodiscard]] ScratchArchive create_scratch_archive();

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::acx::AcxContainer& acx);

[[nodiscard]] std::expected<void, std::string> add_file(
    cricodecs::acx::AcxContainer& acx,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> replace_file(
    cricodecs::acx::AcxContainer& acx,
    uint32_t index,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> remove_file(
    cricodecs::acx::AcxContainer& acx,
    uint32_t index
);

[[nodiscard]] std::expected<void, std::string> move_file(
    cricodecs::acx::AcxContainer& acx,
    uint32_t from_index,
    uint32_t to_index
);

[[nodiscard]] std::filesystem::path suggested_path(
    const cricodecs::acx::AcxContainer& acx,
    uint32_t index
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    const cricodecs::acx::AcxContainer& acx
);

} // namespace cristudio::modules::acx
