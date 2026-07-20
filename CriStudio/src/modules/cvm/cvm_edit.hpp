#pragma once

#include "cvm_container.hpp"
#include "modules/transform_detail.hpp"

#include <QString>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cristudio::modules::cvm {

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::cvm::CvmContainer& cvm);

struct MetadataOptions {
    std::string disc_name;
    std::string recording_date;
    std::string media;
    std::string system_identifier;
    std::string volume_identifier;
    std::string volume_set_identifier;
    std::string publisher_identifier;
    std::string data_preparer_identifier;
    std::string application_identifier;
};

struct ImportedScript {
    std::vector<uint8_t> bytes;
    cricodecs::cvm::CvmContainer container;
};

[[nodiscard]] std::expected<void, std::string> extract_all(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save_session_bytes(
    const cricodecs::cvm::CvmContainer& cvm
);

[[nodiscard]] std::expected<ImportedScript, std::string> import_build_script(
    const std::filesystem::path& script_path
);

[[nodiscard]] std::expected<void, std::string> export_build_script(
    const cricodecs::cvm::CvmContainer& cvm,
    const std::filesystem::path& script_path
);

[[nodiscard]] std::expected<void, std::string> set_metadata_options(
    cricodecs::cvm::CvmContainer& cvm,
    const MetadataOptions& options
);

[[nodiscard]] std::expected<uint32_t, std::string> add_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    std::span<const uint8_t> bytes,
    const std::filesystem::path& archive_path
);

[[nodiscard]] std::expected<void, std::string> replace_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> remove_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index
);

[[nodiscard]] std::expected<void, std::string> move_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t from_index,
    uint32_t to_index
);

[[nodiscard]] std::expected<void, std::string> rename_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    const std::filesystem::path& archive_path
);

[[nodiscard]] QString entry_preview(
    const cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
);

} // namespace cristudio::modules::cvm
