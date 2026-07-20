#pragma once

#include "modules/transform_detail.hpp"

#include "aix_container.hpp"

#include <QString>

#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::aix {

inline constexpr int segment_row_kind = 20;
inline constexpr int layer_row_kind = 21;
inline constexpr int payload_row_kind = 2;

struct BuildConfig {
    std::vector<std::vector<std::filesystem::path>> segments;
    std::filesystem::path output_path;
};

using BuildLogCallback = std::function<void(QString)>;

[[nodiscard]] std::expected<void, std::string> extract_all(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    const cricodecs::aix::Aix& aix
);

[[nodiscard]] std::expected<void, QString> build_from_adx_segments(
    BuildConfig config,
    BuildLogCallback log
);

[[nodiscard]] std::vector<TransformDetailRow> build_job_detail_rows();
[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::aix::Aix& aix);
[[nodiscard]] std::expected<QString, QString> segment_payload_preview(
    const cricodecs::aix::Aix& aix,
    int index,
    int layer
);

} // namespace cristudio::modules::aix
