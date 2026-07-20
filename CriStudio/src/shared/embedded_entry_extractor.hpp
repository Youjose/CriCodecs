#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace cristudio {

enum class EmbeddedPayloadPurpose : uint8_t {
    Raw,
    Playback
};

class EmbeddedEntryExtractor {
public:
    EmbeddedEntryExtractor();
    ~EmbeddedEntryExtractor();

    EmbeddedEntryExtractor(EmbeddedEntryExtractor&&) noexcept;
    EmbeddedEntryExtractor& operator=(EmbeddedEntryExtractor&&) noexcept;

    EmbeddedEntryExtractor(const EmbeddedEntryExtractor&) = delete;
    EmbeddedEntryExtractor& operator=(const EmbeddedEntryExtractor&) = delete;

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract(
        const EntrySummary& entry,
        const DecryptionKeys& keys,
        EmbeddedPayloadPurpose purpose = EmbeddedPayloadPurpose::Raw
    );

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_embedded_entry_payload(
    const EntrySummary& entry,
    const DecryptionKeys& keys,
    EmbeddedPayloadPurpose purpose = EmbeddedPayloadPurpose::Raw
);

} // namespace cristudio
