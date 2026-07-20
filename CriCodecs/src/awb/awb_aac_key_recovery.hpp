#pragma once
/**
 * @file awb_aac_key_recovery.hpp
 * @brief Effective-key recovery for CRI-encrypted M4A payloads in AWB files.
 */

#include "../key_recovery/key_recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cricodecs::awb {

struct AacRecoverySource {
    std::span<const uint8_t> bytes;
};

struct KeyCandidate {
    /// Canonical low-52-bit key. Bits 52-63 are not consumed by the cipher.
    uint64_t key = 0;
    /// Normalized MP4 structural agreement in [0, 1]; not a probability.
    float score = 0.0f;
    /// Inputs that passed the complete supported CRI M4A grammar check.
    size_t validated_sources = 0;
    /// Total same-key inputs supplied by the caller.
    size_t source_count = 0;
    /// Candidates surviving exact top-level MP4 atom bounds.
    size_t candidate_count = 0;
};

struct KeyRecoveryResult {
    std::vector<KeyCandidate> candidates;
    size_t source_count = 0;
    size_t evidence_count = 0;
};

/// Recover the effective AAC key from one or more same-key encrypted M4A
/// payloads. The current deterministic solver supports CRI's reviewed
/// `ftyp`, `free`, `mdat`, `moov` layout and rejects other AAC layouts.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_aac_key(
    std::span<const AacRecoverySource> sources);

} // namespace cricodecs::awb
