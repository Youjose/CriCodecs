#pragma once

/**
 * @file ahx_key_recovery.hpp
 * @brief Structural key recovery for encrypted AHX streams.
 */

#include "ahx_codec.hpp"
#include "../key_recovery/key_recovery.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::ahx {

struct AhxRecoverySource {
    std::span<const uint8_t> bytes;
};

struct AhxKeyCandidate {
    AhxKey key{};
    float score{};
    size_t source_count{};
    uint64_t evidence_count{};
    uint64_t evidence_frames{};
    std::array<uint32_t, 3> candidate_counts{};
    uint64_t canonical_type9_code{};
};

struct AhxRecoveryResult {
    AhxKey key{};
    uint8_t encryption_type{};
    /// Normalized structural confidence in [0, 1]; this is not a probability.
    float score{};
    uint64_t evidence_frames{};
    uint64_t total_frames{};
    std::vector<uint64_t> source_frames;
    /// Frames selecting start, mult, and add, respectively.
    std::array<uint64_t, 3> component_frames{};
    /// Equally ranked values remaining for start, mult, and add.
    std::array<uint32_t, 3> candidate_counts{};
    /// Canonical subkey-zero type-9 keycode for the effective triplet.
    uint64_t canonical_type9_code{};
    /// Ranked public candidates; sparse component ambiguity is retained below.
    std::vector<AhxKeyCandidate> candidates;
    size_t source_count{};
    uint64_t evidence_count{};
};

using KeyCandidate = AhxKeyCandidate;
using KeyRecoveryResult = AhxRecoveryResult;

/// Recover an effective triplet from one or more same-key encrypted AHX streams.
/// Sparse inputs may leave equivalent candidates; the result still returns the
/// best triplet and exposes that ambiguity through score and candidate_counts.
[[nodiscard]] std::expected<AhxRecoveryResult, std::string> recover_key(
    std::span<const AhxRecoverySource> sources);

/// Recover one shared triplet or aggregate independently recovered triplets.
[[nodiscard]] std::expected<AhxRecoveryResult, std::string> recover_key(
    std::span<const AhxRecoverySource> sources,
    KeyRecoveryMode mode);

} // namespace cricodecs::ahx
