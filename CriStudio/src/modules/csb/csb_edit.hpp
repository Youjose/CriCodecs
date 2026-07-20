#pragma once

#include "modules/transform_detail.hpp"

#include "csb_container.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <QString>

namespace cristudio::modules::csb {

struct DirectoryBuildConfig {
    std::filesystem::path input_dir;
    std::filesystem::path output_path;
};

using BuildLogCallback = std::function<void(QString)>;

[[nodiscard]] std::expected<void, QString> build_from_directory(
    DirectoryBuildConfig config,
    BuildLogCallback log
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    const cricodecs::csb::CsbContainer& csb
);

[[nodiscard]] std::vector<TransformDetailRow> build_job_detail_rows();
[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::csb::CsbContainer& csb);
[[nodiscard]] std::expected<QString, QString> payload_preview(
    const cricodecs::csb::CsbContainer& csb,
    int payload_kind,
    int index
);
[[nodiscard]] std::expected<cricodecs::utf::UtfTable, QString> payload_table(
    const cricodecs::csb::CsbContainer& csb,
    int payload_kind,
    int index
);

} // namespace cristudio::modules::csb
