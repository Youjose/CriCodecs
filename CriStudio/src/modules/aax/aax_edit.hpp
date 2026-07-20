#pragma once

#include "modules/transform_detail.hpp"

#include "aax_container.hpp"

#include <QString>

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace cristudio::modules::aax {

struct BuildConfig {
    std::vector<std::filesystem::path> segments;
    std::filesystem::path output_path;
    bool mark_last_segment_as_loop = false;
};

using BuildLogCallback = std::function<void(QString)>;

[[nodiscard]] std::expected<void, QString> build_from_adx_segments(
    BuildConfig config,
    BuildLogCallback log
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    cricodecs::aax::AaxContainer& aax
);

[[nodiscard]] std::vector<TransformDetailRow> build_job_detail_rows();
[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::aax::AaxContainer& aax);
[[nodiscard]] std::expected<QString, QString> segment_payload_preview(
    const cricodecs::aax::AaxContainer& aax,
    int index
);

} // namespace cristudio::modules::aax
