#pragma once
/**
 * @file hca_key_recovery.hpp
 * @brief HCA type-56 effective-key recovery.
 */

#include "../key_recovery/key_recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace cricodecs::hca {

class Hca;

struct RecoverySource {
    const Hca* hca = nullptr;
    uint16_t subkey = 0;
    size_t group = 0;
};

using KeyCandidate = cricodecs::KeyCandidate<uint64_t>;
using KeyRecoveryResult = cricodecs::KeyRecoveryResult<uint64_t>;

/// Recover ranked effective low-56 candidates from one HCA object.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(const Hca& source);

/// Pool compatible HCA objects that use the same effective key.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(std::span<const Hca> sources);

/// Recover base-key candidates while preserving each source's AWB subkey and
/// logical input group. Shared mode asserts that every group has one base key;
/// independent mode recovers groups separately and aggregates equal classes.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(
    std::span<const RecoverySource> sources,
    KeyRecoveryMode mode = KeyRecoveryMode::SharedBaseKey);

} // namespace cricodecs::hca
