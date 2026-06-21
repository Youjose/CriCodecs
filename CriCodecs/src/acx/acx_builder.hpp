#pragma once
/**
 * @file acx_builder.hpp
 * @brief ACX builder API for the reviewed flat offset-size table format.
 *
 * The supported layout follows official `adxcat` binary. The public C++23
 * builder shape is CriCodecs work by Youjose.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::acx {

struct AcxBuildEntry {
    std::filesystem::path source_path;
    std::optional<std::vector<uint8_t>> data;
};

struct AcxBuildInput {
    std::vector<AcxBuildEntry> entries;
    uint32_t alignment = 4;
};

class AcxBuilder {
public:
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build(const AcxBuildInput& input) const;
    [[nodiscard]] std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const AcxBuildInput& input
    ) const;

    [[nodiscard]] static std::expected<AcxBuildInput, std::string> parse_file_list(
        const std::filesystem::path& file_list_path,
        uint32_t alignment = 4
    );
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_from_file_list(
        const std::filesystem::path& file_list_path,
        uint32_t alignment = 4
    ) const;
    [[nodiscard]] std::expected<void, std::string> build_file_list_to_file(
        const std::filesystem::path& file_list_path,
        const std::filesystem::path& output_path,
        uint32_t alignment = 4
    ) const;
};

} // namespace cricodecs::acx
