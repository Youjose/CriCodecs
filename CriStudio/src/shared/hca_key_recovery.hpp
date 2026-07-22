#pragma once

#include "document/document_types.hpp"
#include <hca_key_recovery.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace cristudio {

struct HcaRecoverySource {
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

struct HcaKeyRecoveryResult {
    cricodecs::hca::KeyRecoveryResult recovery;
    size_t hca_count = 0;
};

[[nodiscard]] HcaRecoverySource make_hca_recovery_source(const LoadedDocument& document);
[[nodiscard]] HcaRecoverySource make_hca_recovery_source(const EntrySummary& entry);
[[nodiscard]] bool supports_hca_key_recovery(const LoadedDocument& document);
[[nodiscard]] bool supports_hca_key_recovery(const EntrySummary& entry);

[[nodiscard]] std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> sources,
    const DecryptionKeys& keys = {},
    cricodecs::KeyRecoveryMode mode = cricodecs::KeyRecoveryMode::SharedBaseKey
);

[[nodiscard]] std::expected<HcaKeyRecoveryResult, std::string> recover_hca_key(
    std::span<const HcaRecoverySource> sources,
    const DecryptionKeys& keys,
    const cricodecs::hca::KeyRecoveryOptions& options
);

} // namespace cristudio
