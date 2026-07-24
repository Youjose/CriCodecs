#pragma once
/**
 * @file hca_key_recovery.hpp
 * @brief HCA type-56 effective-key recovery.
 */

#include "../key_recovery/key_recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <stop_token>
#include <string>

namespace cricodecs::hca {

class Hca;

struct RecoverySource {
    const Hca* hca = nullptr;
    uint16_t subkey = 0;
    size_t group = 0;
};

enum class KeyRecoveryStage : uint8_t {
    Collecting,
    Recovering,
};

struct KeyRecoveryProgress {
    KeyRecoveryStage stage = KeyRecoveryStage::Recovering;
    size_t completed = 0;
    size_t total = 0;
    size_t source_count = 0;
    size_t resolved_groups = 0;
};

struct KeyRecoveryOptions {
    KeyRecoveryMode mode = KeyRecoveryMode::SharedBaseKey;
    size_t worker_count = 0;
    std::stop_token stop_token;
    std::function<void(const KeyRecoveryProgress&)> progress;
};

using KeyCandidate = cricodecs::KeyCandidate<uint64_t>;
using KeyRecoveryResult = cricodecs::KeyRecoveryResult<uint64_t>;

/// HCA type-56 table generation consumes only bits 0 through 55 of the
/// effective key. Recovery therefore returns canonical low-56 values; the
/// original caller key's upper byte cannot be inferred from HCA data.
///
/// `KeyCandidate::unknown_high_bits` describes a separate ambiguity inside
/// those 56 observable bits when an AWB subkey cannot be uniquely inverted.

/// Recover ranked canonical low-56 candidates from one HCA object.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(const Hca& source);

/// Pool compatible HCA objects that use the same effective key.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(std::span<const Hca> sources);

/// Recover base-key candidates while preserving each source's AWB subkey and
/// logical input group. Shared mode ranks compatible consensus across groups
/// without discarding isolated outliers; independent mode recovers logical
/// groups separately and aggregates equal classes.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(
    std::span<const RecoverySource> sources,
    KeyRecoveryMode mode = KeyRecoveryMode::SharedBaseKey);

/// Recover base-key candidates with bounded parallel group processing,
/// cooperative cancellation, and optional progress reporting.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(
    std::span<const RecoverySource> sources,
    const KeyRecoveryOptions& options);

} // namespace cricodecs::hca
