#pragma once

#include "document/document_types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

struct AacRecoverySource {
    enum class Kind : uint8_t {
        Document,
        Entry,
    };

    Kind kind = Kind::Document;
    std::filesystem::path path;
    std::string name;
    std::string format;
    std::string loader_tag;
    EntrySummary entry;
};

struct AacKeyCandidateResult {
    uint64_t key = 0;
    float score = 0.0f;
    size_t validated_sources = 0;
    size_t source_count = 0;
    size_t candidate_count = 0;
};

struct AacKeyRecoveryResult {
    std::vector<AacKeyCandidateResult> candidates;
    uint64_t key = 0;
    float score = 0.0f;
    size_t validated_sources = 0;
    size_t source_count = 0;
    size_t container_count = 0;
    size_t candidate_count = 0;
};

[[nodiscard]] bool supports_aac_key_recovery(const LoadedDocument& document);
[[nodiscard]] bool supports_aac_key_recovery(const EntrySummary& entry);
[[nodiscard]] AacRecoverySource make_aac_recovery_source(const LoadedDocument& document);
[[nodiscard]] AacRecoverySource make_aac_recovery_source(const EntrySummary& entry);
[[nodiscard]] std::expected<AacKeyRecoveryResult, std::string> recover_aac_key(
    std::span<const AacRecoverySource> sources);

} // namespace cristudio
