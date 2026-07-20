#include "modules/usm/usm_extract.hpp"

#include "shared/document_extract_helpers.hpp"
#include "shared/document_helpers.hpp"
#include "shared/document_preview_router.hpp"
#include "usm_container.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cristudio::modules::usm {

bool extract_sbt_sidecars(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir,
    ExtractionContext& context,
    ExtractionReport& report,
    const ExtractionOptions& options
) {
    if (!is_sbt_entry(entry)) {
        return false;
    }

    auto cues = cricodecs::usm::parse_sbt_subtitles(bytes);
    if (!cues) {
        return false;
    }

    const auto label = entry.name.empty() ? std::string("SBT subtitles") : entry.name;
    const auto base = output_dir / without_extension(safe_relative_path(entry.name.empty() ? "subtitles.sbt" : entry.name));
    const auto write_bytes = [&](std::filesystem::path path, std::span<const uint8_t> payload, std::string_view output_label) {
        ++report.total;
        path = context.output_paths.allocate(path);
        if (auto written = write_binary_file(path, payload); !written) {
            add_report_failure(report, output_label, written.error(), options);
        } else {
            add_report_success(report, path, output_label, options);
        }
    };
    const auto write_text = [&](std::filesystem::path path, std::string_view text, std::string_view output_label) {
        ++report.total;
        path = context.output_paths.allocate(path);
        if (auto written = write_text_file(path, text); !written) {
            add_report_failure(report, output_label, written.error(), options);
        } else {
            add_report_success(report, path, output_label, options);
        }
    };

    write_bytes(with_extension(base, ".sbt"), bytes, label + " raw SBT");
    if (auto source = cricodecs::usm::sbt_to_subtitle_source_text(bytes)) {
        write_text(with_stem_suffix(base, "_source", ".txt"), *source, label + " source text");
    } else {
        ++report.total;
        add_report_failure(report, label + " source text", source.error(), options);
    }
    if (auto tracks = cricodecs::usm::sbt_to_srt_tracks(bytes)) {
        for (const auto& [language_id, srt] : *tracks) {
            write_text(
                with_stem_suffix(base, "_lang" + number(language_id), ".srt"),
                srt,
                label + " SRT language " + number(language_id)
            );
        }
    } else {
        ++report.total;
        add_report_failure(report, label + " SRT", tracks.error(), options);
    }
    if (auto ass = cricodecs::usm::sbt_to_ass(bytes, archive_leaf_name(label))) {
        write_text(with_extension(base, ".ass"), *ass, label + " ASS");
    } else {
        ++report.total;
        add_report_failure(report, label + " ASS", ass.error(), options);
    }
    return true;
}

} // namespace cristudio::modules::usm
