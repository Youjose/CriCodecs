#include "shared/adx_key_recovery.hpp"

#include "shared/embedded_entry_extractor.hpp"

#include <adx_key_recovery.hpp>
#include <ahx_key_recovery.hpp>
#include "io_reader.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include <utility>

namespace cristudio {
namespace {

[[nodiscard]] std::string lower_ascii(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool mentions_adx_family(std::string_view text) {
    const auto lower = lower_ascii(std::string(text));
    return lower.find("adx") != std::string::npos || lower.find("ahx") != std::string::npos;
}

[[nodiscard]] bool source_might_be_adx_family(const AdxRecoverySource& source) {
    if (source.kind == AdxRecoverySource::Kind::Document) {
        return mentions_adx_family(source.name) || mentions_adx_family(source.format) ||
            mentions_adx_family(source.loader_tag);
    }
    return mentions_adx_family(source.entry.name) || mentions_adx_family(source.entry.type) ||
        mentions_adx_family(source.entry.detail);
}

[[nodiscard]] bool is_requested_payload(std::span<const uint8_t> bytes, AdxRecoveryKind kind) {
    if (bytes.size() <= 19u || bytes[0] != 0x80u || bytes[1] != 0x00u) {
        return false;
    }
    const bool is_ahx = bytes[4] == 0x10u || bytes[4] == 0x11u;
    return is_ahx == (kind == AdxRecoveryKind::Ahx) && (bytes[19] == 8u || bytes[19] == 9u);
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_source(
    const AdxRecoverySource& source,
    EmbeddedEntryExtractor& extractor,
    const DecryptionKeys& keys
) {
    if (source.kind == AdxRecoverySource::Kind::Entry) {
        return extractor.extract(source.entry, keys, EmbeddedPayloadPurpose::Raw);
    }
    return cricodecs::io::read_file_bytes(source.path, "ADX/AHX key recovery failed");
}

} // namespace

AdxRecoverySource make_adx_recovery_source(const LoadedDocument& document) {
    return AdxRecoverySource{
        .kind = AdxRecoverySource::Kind::Document,
        .path = document.path,
        .name = document.display_name,
        .format = document.format,
        .loader_tag = document.loader_tag,
    };
}

AdxRecoverySource make_adx_recovery_source(const EntrySummary& entry) {
    EntrySummary compact{
        .name = entry.name,
        .type = entry.type,
        .detail = entry.detail,
        .source_path = entry.source_path,
        .source_format = entry.source_format,
        .source_index = entry.source_index,
        .has_source = entry.has_source,
        .nested_source_format = entry.nested_source_format,
        .nested_source_index = entry.nested_source_index,
        .has_nested_source = entry.has_nested_source,
        .hca_subkey = entry.hca_subkey,
    };
    return AdxRecoverySource{
        .kind = AdxRecoverySource::Kind::Entry,
        .name = entry.name,
        .entry = std::move(compact),
    };
}

std::expected<AdxKeyRecoveryResult, std::string> recover_adx_key(
    std::span<const AdxRecoverySource> inputs,
    AdxRecoveryKind kind,
    const DecryptionKeys& keys
) {
    std::vector<std::vector<uint8_t>> bytes;
    EmbeddedEntryExtractor extractor;
    for (const auto& input : inputs) {
        if (!source_might_be_adx_family(input)) {
            continue;
        }
        auto source = read_source(input, extractor, keys);
        if (!source) {
            return std::unexpected("ADX/AHX key recovery failed: " + source.error());
        }
        if (is_requested_payload(*source, kind)) {
            bytes.push_back(std::move(*source));
        }
    }

    const auto label = kind == AdxRecoveryKind::Ahx ? std::string_view("AHX") : std::string_view("ADX");
    if (bytes.empty()) {
        return std::unexpected("No encrypted " + std::string(label) + " streams were found in the selected files.");
    }

    if (kind == AdxRecoveryKind::Adx) {
        std::vector<cricodecs::adx::AdxRecoverySource> sources;
        sources.reserve(bytes.size());
        for (const auto& source : bytes) {
            sources.push_back({.bytes = source});
        }
        auto recovered = cricodecs::adx::recover_key(sources);
        if (!recovered) {
            return std::unexpected(recovered.error());
        }
        AdxKeyRecoveryResult result;
        result.start = recovered->key.xor_value;
        result.mult = recovered->key.mult;
        result.add = recovered->key.add;
        result.encryption_type = recovered->encryption_type;
        result.score = recovered->score;
        result.source_count = sources.size();
        result.total_frames = recovered->total_frames;
        result.examined_frames = recovered->examined_frames;
        result.evidence_frames = recovered->evidence_frames;
        result.source_frames = std::move(recovered->source_frames);
        result.canonical_type9_code = recovered->canonical_type9_code;
        result.candidates.reserve(recovered->candidates.size());
        for (const auto& candidate : recovered->candidates) {
            result.candidates.push_back(AdxKeyCandidateResult{
                .start = candidate.key.xor_value,
                .mult = candidate.key.mult,
                .add = candidate.key.add,
                .score = candidate.score,
                .source_count = candidate.source_count,
                .evidence_count = candidate.evidence_count,
                .canonical_type9_code = candidate.canonical_type9_code,
            });
        }
        return result;
    }

    std::vector<cricodecs::ahx::AhxRecoverySource> sources;
    sources.reserve(bytes.size());
    for (const auto& source : bytes) {
        sources.push_back({.bytes = source});
    }
    auto recovered = cricodecs::ahx::recover_key(sources);
    if (!recovered) {
        return std::unexpected(recovered.error());
    }
    AdxKeyRecoveryResult result;
    result.start = recovered->key.start;
    result.mult = recovered->key.mult;
    result.add = recovered->key.add;
    result.encryption_type = recovered->encryption_type;
    result.score = recovered->score;
    result.source_count = sources.size();
    result.total_frames = recovered->total_frames;
    result.evidence_frames = recovered->evidence_frames;
    result.source_frames = std::move(recovered->source_frames);
    result.component_frames = recovered->component_frames;
    result.candidate_counts = recovered->candidate_counts;
    result.canonical_type9_code = recovered->canonical_type9_code;
    result.candidates.reserve(recovered->candidates.size());
    for (const auto& candidate : recovered->candidates) {
        result.candidates.push_back(AdxKeyCandidateResult{
            .start = candidate.key.start,
            .mult = candidate.key.mult,
            .add = candidate.key.add,
            .score = candidate.score,
            .source_count = candidate.source_count,
            .evidence_count = candidate.evidence_count,
            .candidate_counts = candidate.candidate_counts,
            .canonical_type9_code = candidate.canonical_type9_code,
        });
    }
    return result;
}

} // namespace cristudio
