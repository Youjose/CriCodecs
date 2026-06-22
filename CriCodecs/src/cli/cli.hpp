#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::cli {

enum class Format {
    aax,
    acb,
    acx,
    adx,
    afs,
    ahx,
    aix,
    awb,
    cpk,
    csb,
    cvm,
    hca,
    sfd,
    usm,
    utf,
    wav,
    video,
};

[[nodiscard]] std::string_view format_key(Format format) noexcept;
[[nodiscard]] std::string_view format_label(Format format) noexcept;
[[nodiscard]] std::optional<Format> parse_format_key(std::string_view text) noexcept;
[[nodiscard]] bool format_supported_in_cli(Format format) noexcept;
[[nodiscard]] bool format_supports_default_write(Format format) noexcept;

[[nodiscard]] int error_suspicion(std::string_view message);
[[nodiscard]] std::vector<Format> fallback_probe_order(bool include_riff_wave = false);
[[nodiscard]] std::vector<Format> sniff_format_order(
    std::span<const uint8_t> bytes,
    bool include_riff_wave = false
);
[[nodiscard]] std::vector<Format> sniff_format_order(
    const std::filesystem::path& path,
    bool include_riff_wave = false
);

int run(std::span<const std::string> args, std::ostream& out, std::ostream& err);

} // namespace cricodecs::cli
