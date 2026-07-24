#pragma once

/**
 * @file adx_key_recovery.hpp
 * @brief Structural ADX type-8 and type-9 effective-triplet recovery.
 */

#include "adx_crypto.hpp"
#include "../key_recovery/key_recovery.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::adx {

struct AdxRecoverySource {
    std::span<const uint8_t> bytes;
};

struct AdxKeyCandidate {
    AdxKeyState key{};
    float score{};
    size_t source_count{};
    uint64_t evidence_count{};
    uint64_t evidence_frames{};
    uint64_t canonical_type9_code{};
};

struct AdxRecoveryResult {
    AdxKeyState key{};
    uint8_t encryption_type{};
    /// Normalized validation agreement in [0, 1]; this is not a probability.
    float score{};
    /// Frame positions inspected while validating the returned candidate.
    uint64_t examined_frames{};
    /// Non-empty frames that supplied structural scale-bit evidence.
    uint64_t evidence_frames{};
    /// Total audio frames available across all same-key inputs.
    uint64_t total_frames{};
    /// Total audio frames available in each input, in caller order.
    std::vector<uint64_t> source_frames;
    // Any effective triplet that decrypts the supplied stream(s) is valid;
    // this need not be the original authoring key.
    uint64_t canonical_type9_code{};
    /// Ranked public candidates; currently deterministic paths may return one.
    std::vector<AdxKeyCandidate> candidates;
    size_t source_count{};
    uint64_t evidence_count{};
};

using KeyCandidate = AdxKeyCandidate;
using KeyRecoveryResult = AdxRecoveryResult;

/// Recover an effective triplet from one or more same-key encrypted ADX
/// streams. The encryption type is detected from the ADX header.
[[nodiscard]] std::expected<AdxRecoveryResult, std::string> recover_key(
    std::span<const AdxRecoverySource> sources);

/// Recover one shared effective triplet or aggregate independently recovered
/// triplets. The encryption type of each input is detected automatically.
[[nodiscard]] std::expected<AdxRecoveryResult, std::string> recover_key(
    std::span<const AdxRecoverySource> sources,
    KeyRecoveryMode mode);

} // namespace cricodecs::adx
