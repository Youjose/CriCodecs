#pragma once
/**
 * @file usm_key_recovery.hpp
 * @brief Best-effort recovery of the effective USM mask key.
 */

#include "../key_recovery/key_recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace cricodecs::usm {

class UsmReader;

struct KeyCandidate {
    /// Effective low-56 mask key. The original upper byte is not recoverable.
    uint64_t key = 0;
    /// Fraction of modeled byte and byte-pair observations matched by the guess.
    float score = 0.0f;
    size_t source_count = 1;
    size_t evidence_count = 0;
    /// Number of key-dependent block observations used to rank the guess.
    size_t sample_blocks = 0;
    /// Number of video or alpha-video chunks that contributed evidence.
    size_t video_chunks = 0;
    /// Number of ADX audio chunks that contributed known or modeled bytes.
    size_t audio_chunks = 0;
    /// Confidence of the ADX audio-mask candidate, or zero when unavailable.
    float audio_score = 0.0f;
    /// Number of cipher-56 HCA streams pooled for this candidate.
    size_t hca_streams = 0;
    /// Score reported by HCA recovery, or zero when this key did not come from HCA evidence.
    float hca_score = 0.0f;
    /// True when the HCA key also met the video-support threshold and influenced ranking.
    bool hca_video_supported = false;
};

struct KeyRecoveryResult {
    std::vector<KeyCandidate> candidates;
    size_t source_count = 1;
    size_t evidence_count = 0;
};

/// Recover a best-effort key guess without changing the reader's configured key.
[[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_key(const UsmReader& source);

} // namespace cricodecs::usm
