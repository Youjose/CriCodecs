#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::adx {

enum class RecoveryStreamKind : uint8_t {
    Adx,
    Ahx,
};

[[nodiscard]] std::expected<std::vector<std::vector<uint8_t>>, std::string>
collect_recovery_streams(
    const std::filesystem::path& path,
    RecoveryStreamKind kind);

[[nodiscard]] std::expected<std::vector<std::vector<uint8_t>>, std::string>
collect_recovery_streams(
    std::span<const uint8_t> bytes,
    RecoveryStreamKind kind);

} // namespace cricodecs::adx
