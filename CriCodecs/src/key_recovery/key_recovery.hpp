#pragma once
/**
 * @file key_recovery.hpp
 * @brief Shared key-recovery policy and bounded result helpers.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cricodecs {

inline constexpr size_t MaxKeyRecoveryCandidates = 10;

enum class KeyRecoveryMode : uint8_t {
    Independent,
    SharedBaseKey,
};

template <typename Key>
struct KeyCandidate {
    Key key{};
    float score = 0.0f;
    size_t source_count = 0;
    size_t evidence_count = 0;
    uint8_t unknown_high_bits = 0;
    uint32_t equivalent_count = 1;
};

template <typename Key>
struct KeyRecoveryResult {
    std::vector<KeyCandidate<Key>> candidates;
    size_t source_count = 0;
    size_t evidence_count = 0;
};

template <typename Key, typename Better>
void retain_key_candidates(std::vector<KeyCandidate<Key>>& candidates, Better better) {
    std::ranges::stable_sort(candidates, better);
    if (candidates.size() > MaxKeyRecoveryCandidates) {
        candidates.resize(MaxKeyRecoveryCandidates);
    }
}

} // namespace cricodecs
