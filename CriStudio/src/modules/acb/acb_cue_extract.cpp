#include "modules/acb/acb_cue_extract.hpp"

#include "shared/document_extraction_report.hpp"
#include "shared/document_helpers.hpp"
#include "shared/i18n.hpp"

#include "acb_container.hpp"
#include "acb_cue_renderer.hpp"
#include "acb_cue_resolver.hpp"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

namespace cristudio::modules::acb {
namespace {

cricodecs::acb::AcbCueRenderOptions render_options(
    const DecryptionKeys& keys,
    bool include_empty_holds = false) {
    return {
        .include_empty_infinite_blocks = include_empty_holds,
        .hca_keycode = keys.has_cri_key ? keys.cri_key : 0,
        .hca_subkey = keys.hca_subkey == 0
            ? std::nullopt
            : std::optional<uint16_t>{keys.hca_subkey},
    };
}

bool canceled(
    ExtractionReport& report,
    const ExtractionOptions& options) {
    if (!options.stop_token.stop_requested()) {
        return false;
    }
    report.canceled = true;
    return true;
}

bool prepare_output_directory(
    const std::filesystem::path& output_dir,
    std::string_view label,
    const ExtractionOptions& options,
    ExtractionReport& report) {
    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    if (!error) {
        return true;
    }
    add_report_failure(
        report,
        label,
        cristudio::i18n::translate_utf8(
            "DocumentLoader",
            "could not create ACB cue output directory: ") +
            error.message(),
        options);
    return false;
}

} // namespace

void extract_cue_sheet_into_report(
    const LoadedDocument& document,
    const std::filesystem::path& output_dir,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    OutputPathAllocator& output_paths,
    ExtractionReport& report) {
    const auto label = document.display_name.empty()
        ? filename_of(document.path)
        : document.display_name;
    if (!prepare_output_directory(output_dir, label, options, report)) {
        ++report.total;
        return;
    }
    auto acb = cricodecs::acb::AcbContainer::load(document.path);
    if (!acb) {
        ++report.total;
        add_report_failure(report, label, acb.error(), options);
        return;
    }
    auto resolution = cricodecs::acb::resolve_cue_sheet_playback(*acb);
    if (!resolution) {
        ++report.total;
        add_report_failure(report, label, resolution.error(), options);
        return;
    }
    if (resolution->plans.empty()) {
        ++report.total;
        add_report_failure(
            report,
            label,
            cristudio::i18n::translate_utf8(
                "DocumentLoader",
                "ACB contains no statically resolvable cue plans"),
            options);
        return;
    }
    const auto filenames =
        cricodecs::acb::cue_plan_filenames(*resolution);
    const auto cue_options = render_options(keys);
    for (size_t index = 0; index < resolution->plans.size(); ++index) {
        if (canceled(report, options)) {
            return;
        }
        ++report.total;
        const auto output_path = output_paths.allocate(
            output_dir / filenames[index]);
        auto extracted = cricodecs::acb::extract_cue_plan(
            *acb,
            resolution->plans[index].plan,
            output_path,
            cue_options);
        if (!extracted) {
            add_report_failure(
                report,
                resolution->plans[index].plan.cue_name,
                extracted.error(),
                options);
            continue;
        }
        add_report_success(
            report,
            output_path,
            resolution->plans[index].plan.cue_name,
            options);
    }
}

void extract_cue_target_into_report(
    const ExtractionTarget& target,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    OutputPathAllocator& output_paths,
    ExtractionReport& report) {
    const auto label = target.acb_output_name.empty()
        ? std::string("ACB cue")
        : target.acb_output_name;
    ++report.total;
    if (mode == ExtractionMode::Raw) {
        add_report_failure(
            report,
            label,
            cristudio::i18n::translate_utf8(
                "DocumentLoader",
                "raw extraction is unavailable for a rendered ACB cue"),
            options);
        return;
    }
    if (!prepare_output_directory(output_dir, label, options, report)) {
        return;
    }
    auto acb = cricodecs::acb::AcbContainer::load(target.acb_path);
    if (!acb) {
        add_report_failure(report, label, acb.error(), options);
        return;
    }
    auto resolution = cricodecs::acb::resolve_cue_playback_paths(
        *acb,
        target.acb_cue_index);
    if (!resolution) {
        add_report_failure(report, label, resolution.error(), options);
        return;
    }
    const auto& graph = acb->cue_graph();
    const auto selected = std::ranges::find_if(
        resolution->plans,
        [&](const cricodecs::acb::AcbResolvedCuePlan& candidate) {
            return cricodecs::acb::cue_plan_semantic_signature(
                       graph,
                       candidate.plan) == target.acb_plan_signature;
        });
    if (selected == resolution->plans.end()) {
        add_report_failure(
            report,
            label,
            cristudio::i18n::translate_utf8(
                "DocumentLoader",
                "selected ACB cue path is no longer available"),
            options);
        return;
    }
    auto plan = selected->plan;
    for (auto& block : plan.blocks) {
        if (block.authored_loop_count < 0 && block.clips.empty()) {
            block.skipped_empty_hold = !target.acb_include_empty_holds;
            block.render_loop_count = 0;
        }
    }
    const auto output_path = output_paths.allocate(
        output_dir /
        (target.acb_output_name.empty()
             ? cricodecs::acb::cue_filename(
                   *acb,
                   target.acb_cue_index)
             : target.acb_output_name));
    auto extracted = cricodecs::acb::extract_cue_plan(
        *acb,
        std::move(plan),
        output_path,
        render_options(keys, target.acb_include_empty_holds));
    if (!extracted) {
        add_report_failure(report, label, extracted.error(), options);
        return;
    }
    add_report_success(report, output_path, label, options);
}

} // namespace cristudio::modules::acb
