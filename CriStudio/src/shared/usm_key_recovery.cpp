#include "shared/usm_key_recovery.hpp"

#include "shared/embedded_entry_extractor.hpp"

#include "usm_container.hpp"
#include "usm_key_recovery.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

[[nodiscard]] std::string lower_ascii(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool is_usm(std::span<const uint8_t> bytes) {
    constexpr std::string_view crid = "CRID";
    constexpr std::string_view sfsh = "SFSH";
    return bytes.size() >= 4u &&
        (std::equal(crid.begin(), crid.end(), bytes.begin()) ||
         std::equal(sfsh.begin(), sfsh.end(), bytes.begin()));
}

void append_guess(
    const cricodecs::usm::UsmReader& usm,
    std::string label,
    UsmKeyRecoveryReport& report
) {
    auto guess = cricodecs::usm::recover_key(usm);
    if (!guess) {
        report.errors.push_back(std::move(label) + ": " + guess.error());
        return;
    }
    if (guess->candidates.empty()) {
        report.errors.push_back(std::move(label) + ": recovery returned no candidates");
        return;
    }
    for (const auto& candidate : guess->candidates) {
        report.recovered.push_back(UsmKeyRecoveryResult{
            .source = label,
            .key = candidate.key,
            .score = candidate.score,
            .source_count = candidate.source_count,
            .evidence_count = candidate.evidence_count,
            .sample_blocks = candidate.sample_blocks,
            .video_chunks = candidate.video_chunks,
            .audio_chunks = candidate.audio_chunks,
            .audio_score = candidate.audio_score,
            .hca_streams = candidate.hca_streams,
            .hca_score = candidate.hca_score,
            .hca_video_supported = candidate.hca_video_supported,
        });
    }
}

void append_path(
    const std::filesystem::path& path,
    std::string label,
    std::unordered_set<std::string>& seen,
    UsmKeyRecoveryReport& report
) {
    const auto identity = path.lexically_normal().generic_string();
    if (!seen.insert(identity).second) {
        return;
    }
    ++report.usm_count;
    cricodecs::usm::UsmReader usm;
    if (auto loaded = usm.load(path); !loaded) {
        report.errors.push_back(std::move(label) + ": " + loaded.error());
        return;
    }
    append_guess(usm, std::move(label), report);
}

void append_bytes(
    std::span<const uint8_t> bytes,
    std::string label,
    std::string identity,
    std::unordered_set<std::string>& seen,
    UsmKeyRecoveryReport& report
) {
    if (!is_usm(bytes) || !seen.insert(std::move(identity)).second) {
        return;
    }
    ++report.usm_count;
    cricodecs::usm::UsmReader usm;
    if (auto loaded = usm.load(bytes); !loaded) {
        report.errors.push_back(std::move(label) + ": " + loaded.error());
        return;
    }
    append_guess(usm, std::move(label), report);
}

} // namespace

UsmRecoverySource make_usm_recovery_source(const LoadedDocument& document) {
    return UsmRecoverySource{
        .kind = UsmRecoverySource::Kind::Document,
        .path = document.path,
        .name = document.display_name,
        .format = document.format,
        .loader_tag = document.loader_tag,
    };
}

UsmRecoverySource make_usm_recovery_source(const EntrySummary& entry) {
    EntrySummary compact{
        .name = entry.name,
        .source_path = entry.source_path,
        .source_format = entry.source_format,
        .source_index = entry.source_index,
        .has_source = entry.has_source,
        .nested_source_format = entry.nested_source_format,
        .nested_source_index = entry.nested_source_index,
        .has_nested_source = entry.has_nested_source,
        .hca_subkey = entry.hca_subkey,
    };
    return UsmRecoverySource{
        .kind = UsmRecoverySource::Kind::Entry,
        .name = entry.name,
        .entry = std::move(compact),
    };
}

std::expected<UsmKeyRecoveryReport, std::string> recover_usm_keys(
    std::span<const UsmRecoverySource> inputs,
    const DecryptionKeys& keys
) {
    UsmKeyRecoveryReport report;
    std::unordered_set<std::string> seen;
    EmbeddedEntryExtractor extractor;

    for (const auto& input : inputs) {
        const auto label = input.name.empty() ? input.path.filename().generic_string() : input.name;
        if (input.kind == UsmRecoverySource::Kind::Document) {
            const auto tag = lower_ascii(input.loader_tag.empty() ? input.format : input.loader_tag);
            if (tag == "usm" || tag.find("usm/") != std::string::npos) {
                append_path(input.path, label, seen, report);
            }
            continue;
        }

        const auto source_format = lower_ascii(input.entry.source_format);
        if (source_format == "usm" && !input.entry.source_path.empty()) {
            append_path(input.entry.source_path, label, seen, report);
            continue;
        }

        auto bytes = extractor.extract(input.entry, keys, EmbeddedPayloadPurpose::Raw);
        if (!bytes) {
            report.errors.push_back(label + ": " + bytes.error());
            continue;
        }
        const auto identity = input.entry.source_path.lexically_normal().generic_string() + ":" +
            std::to_string(input.entry.source_index) + ":" +
            std::to_string(input.entry.nested_source_index);
        append_bytes(*bytes, label, identity, seen, report);
    }

    if (report.usm_count == 0u) {
        return std::unexpected("No USM containers were found in the selected files or entries.");
    }
    return report;
}

} // namespace cristudio
