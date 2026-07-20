#pragma once
/**
 * @file cvm_key_recovery.hpp
 * @brief Exact recovery of the effective CVM TOC scramble key.
 */

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace cricodecs::cvm {

using CvmKey = std::array<uint8_t, 8>;

/// Recover an effective key from the fixed ISO9660 primary-volume header.
/// The returned bytes decrypt the image but need not encode the original
/// authoring password used to derive them.
[[nodiscard]] std::expected<CvmKey, std::string> recover_key(std::span<const uint8_t> data);

[[nodiscard]] std::expected<CvmKey, std::string> recover_key(const std::filesystem::path& path);

} // namespace cricodecs::cvm
