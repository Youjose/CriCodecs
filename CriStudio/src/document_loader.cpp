#include "document_loader.hpp"

#include "modules/aax/aax_browse.hpp"
#include "modules/acb/acb_browse.hpp"
#include "modules/acx/acx_browse.hpp"
#include "modules/afs/afs_browse.hpp"
#include "modules/adx/adx_browse.hpp"
#include "modules/aix/aix_browse.hpp"
#include "modules/awb/awb_browse.hpp"
#include "modules/csb/csb_browse.hpp"
#include "modules/cpk/cpk_browse.hpp"
#include "modules/cvm/cvm_browse.hpp"
#include "modules/hca/hca_browse.hpp"
#include "modules/sfd/sfd_browse.hpp"
#include "modules/utf/utf_browse.hpp"
#include "modules/usm/usm_browse.hpp"
#include "modules/usm/usm_extract.hpp"
#include "modules/wav/wav_browse.hpp"
#include "path_text.hpp"
#include "shared/document_extract_helpers.hpp"
#include "shared/document_extraction_report.hpp"
#include "shared/document_helpers.hpp"
#include "shared/document_preview_router.hpp"
#include "shared/document_sniffer.hpp"
#include "shared/embedded_document_loader.hpp"
#include "shared/embedded_preview_helpers.hpp"
#include "shared/mux_export_helpers.hpp"
#include "shared/raw_extract_helpers.hpp"
#include "shared/video_probe.hpp"

#include "awb_entry_codec.hpp"

#include "aax_container.hpp"
#include "acb_container.hpp"
#include "acx_container.hpp"
#include "adx_codec.hpp"
#include "afs_container.hpp"
#include "aix_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "csb_container.hpp"
#include "cvm_container.hpp"
#include "hca_codec.hpp"
#include "io_reader.hpp"
#include "sfd_container.hpp"
#include "usm_container.hpp"
#include "utf_table.hpp"
#include "wav_container.hpp"

#include <algorithm>
#include <expected>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace cristudio {
namespace {

std::string display_format_for_loader_tag(std::string_view type) {
    if (type == "cpk") return "CPK archive";
    if (type == "csb") return "CSB cue archive";
    if (type == "acb") return "ACB cue sheet";
    if (type == "awb") return "AWB audio bank";
    if (type == "usm") return "USM/SofDec stream";
    if (type == "sfd") return "SFD/SofDec movie";
    if (type == "sbt") return "USM SBT subtitles";
    if (type == "afs") return "AFS archive";
    if (type == "aax") return "AAX audio wrapper";
    if (type == "aix") return "AIX audio container";
    if (type == "acx") return "ACX archive";
    if (type == "adx") return "ADX audio";
    if (type == "hca") return "HCA audio";
    if (type == "cvm") return "CVM image";
    if (type == "utf") return "UTF table";
    return std::string(type);
}

bool supports_file_backed_metadata_index(std::string_view type) {
    return type == "cpk" || type == "afs" || type == "acx" || type == "awb" ||
        type == "aix" || type == "usm" || type == "sfd";
}

DecryptionKeys preview_keys_for_entry(const EntrySummary& entry, const DecryptionKeys& keys) {
    DecryptionKeys preview_keys = keys;
    if (preview_keys.hca_subkey == 0 && entry.hca_subkey != 0) {
        preview_keys.hca_subkey = entry.hca_subkey;
    }
    return preview_keys;
}

std::expected<AudioPreview, std::string> audio_preview_from_entry_bytes(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    const auto preview_keys = preview_keys_for_entry(entry, keys);

    std::string reason;
    if (auto doc = summarize_embedded_bytes(entry, bytes, reason)) {
        if (is_direct_audio_format(doc->format)) {
            return audio_preview_from_bytes(*doc, bytes, preview_keys);
        }
    }

    LoadedDocument fallback;
    fallback.path = path_from_utf8(entry.name);
    fallback.display_name = entry.name;
    fallback.format = entry.type.empty() ? "audio" : entry.type;
    if (auto audio = audio_preview_from_bytes(fallback, bytes, preview_keys)) {
        return audio;
    }
    return std::unexpected(reason.empty() ? "audio stream is not directly decodable" : reason);
}

bool is_archive_like_document(const LoadedDocument& document) {
    return !document.entries.empty();
}

bool is_mux_video_entry(const EntrySummary& entry) {
    const auto text = lower_ascii(entry.type + " " + entry.source_format + " " + entry.nested_source_format);
    return text.find("video") != std::string::npos ||
        text.find("sfv") != std::string::npos ||
        text.find("mpeg") != std::string::npos ||
        text.find("h.264") != std::string::npos ||
        text.find("h264") != std::string::npos ||
        text.find("ivf") != std::string::npos ||
        text.find("vp9") != std::string::npos;
}

std::expected<std::pair<std::vector<uint8_t>, std::string>, std::string> decoded_entry_payload(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    if (auto audio = audio_preview_from_entry_bytes(entry, bytes, keys)) {
        return std::make_pair(std::move(audio->wav_bytes), std::string(".wav"));
    } else if (is_audio_entry(entry)) {
        return std::unexpected("audio decode failed: " + audio.error());
    }
    return std::make_pair(std::vector<uint8_t>(bytes.begin(), bytes.end()), std::string{});
}

std::expected<std::pair<std::vector<uint8_t>, std::string>, std::string> decoded_document_payload(
    const LoadedDocument& document,
    const DecryptionKeys& keys
) {
    if (is_direct_audio_format(document.format)) {
        auto audio = build_audio_preview(document, keys);
        if (!audio) {
            return std::unexpected(audio.error());
        }
        return std::make_pair(std::move(audio->wav_bytes), std::string(".wav"));
    }

    auto bytes = read_binary_file(document.path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return std::make_pair(std::move(*bytes), std::string{});
}

void extract_entry_into_report(
    const EntrySummary& entry,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    ExtractionContext& context,
    ExtractionReport& report
);

void extract_document_into_report(
    const LoadedDocument& document,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    ExtractionContext& context,
    ExtractionReport& report
) {
    if (is_archive_like_document(document)) {
        const auto base_dir = output_dir / without_extension(safe_document_name(document));
        const auto archive_label = document.display_name.empty() ? filename_of(document.path) : document.display_name;
        add_report_message(
            report,
            "Extracting archive " + archive_label + " (" + std::to_string(document.entries.size()) + " entries)",
            options
        );
        if (mode == ExtractionMode::Decoded && options.include_mux_outputs && is_mux_document_format(document.format)) {
            ++report.total;
            const auto label = archive_label;
            if (auto mux = build_mux_preview(document, options.mux_audio_choice, keys)) {
                auto output_path = context.output_paths.allocate(output_dir / with_extension(safe_document_name(document), ".mkv"));
                if (auto written = write_mux_extract_file(*mux, output_path, options.ffmpeg_path); !written) {
                    add_report_failure(report, label, "mux output unavailable: " + written.error(), options);
                } else {
                    add_report_success(report, output_path, label + " mux output", options);
                }
            } else {
                add_report_failure(report, label, "mux output unavailable: " + mux.error(), options);
            }
        }
        for (const auto& entry : document.entries) {
            extract_entry_into_report(entry, base_dir, mode, keys, options, context, report);
        }
        add_report_message(
            report,
            "Finished archive " + archive_label + " (" + std::to_string(document.entries.size()) + " entries)",
            options
        );
        return;
    }

    ++report.total;
    const auto label = document.display_name.empty() ? filename_of(document.path) : document.display_name;
    std::expected<std::pair<std::vector<uint8_t>, std::string>, std::string> payload =
        std::unexpected("extraction was not started");
    if (mode == ExtractionMode::Raw) {
        auto bytes = read_binary_file(document.path);
        if (!bytes) {
            payload = std::unexpected(bytes.error());
        } else if (auto raw = raw_extract_transform(*bytes, keys); raw) {
            payload = std::make_pair(std::move(*raw), std::string{});
        } else {
            payload = std::unexpected(raw.error());
        }
    } else {
        payload = decoded_document_payload(document, keys);
    }
    if (!payload) {
        add_report_failure(report, label, payload.error(), options);
        return;
    }

    auto output_path = output_dir / safe_document_name(document);
    if (!payload->second.empty()) {
        output_path = with_extension(output_path, payload->second);
    }
    output_path = context.output_paths.allocate(output_path);
    if (auto written = write_binary_file(output_path, payload->first); !written) {
        add_report_failure(report, label, written.error(), options);
        return;
    }
    add_report_success(report, output_path, label, options);
}

void extract_entry_into_report(
    const EntrySummary& entry,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options,
    ExtractionContext& context,
    ExtractionReport& report
) {
    const auto label = entry.name.empty() ? entry.type : entry.name;
    const auto payload_purpose = mode == ExtractionMode::Decoded && is_audio_entry(entry)
        ? EmbeddedPayloadPurpose::Playback
        : EmbeddedPayloadPurpose::Raw;
    auto bytes = context.embedded_entries.extract(entry, keys, payload_purpose);
    if (!bytes) {
        ++report.total;
        add_report_failure(report, label, bytes.error(), options);
        return;
    }

    if (mode == ExtractionMode::Decoded) {
        if (modules::usm::extract_sbt_sidecars(entry, *bytes, output_dir, context, report, options)) {
            return;
        }

        std::string reason;
        if (auto nested_doc = summarize_embedded_bytes(entry, *bytes, reason); nested_doc && is_archive_like_document(*nested_doc)) {
            attach_nested_sources(*nested_doc, entry);
            const auto nested_label = label.empty() ? nested_doc->format : std::string(label);
            add_report_message(
                report,
                "Extracting nested archive " + nested_label + " (" + std::to_string(nested_doc->entries.size()) + " entries)",
                options
            );
            for (const auto& nested_entry : nested_doc->entries) {
                extract_entry_into_report(
                    nested_entry,
                    output_dir / without_extension(safe_relative_path(archive_leaf_name(entry.name))),
                    mode,
                    keys,
                    options,
                    context,
                    report
                );
            }
            add_report_message(
                report,
                "Finished nested archive " + nested_label + " (" + std::to_string(nested_doc->entries.size()) + " entries)",
                options
            );
            return;
        }
    }

    ++report.total;
    std::expected<std::pair<std::vector<uint8_t>, std::string>, std::string> payload =
        std::unexpected("extraction was not started");
    if (mode == ExtractionMode::Raw) {
        if (auto raw = raw_extract_transform(*bytes, keys); raw) {
            payload = std::make_pair(std::move(*raw), std::string{});
        } else {
            payload = std::unexpected(raw.error());
        }
    } else {
        payload = decoded_entry_payload(entry, *bytes, keys);
    }
    if (!payload) {
        add_report_failure(report, label, payload.error(), options);
        return;
    }

    auto output_path = output_dir / safe_relative_path(entry.name);
    if (!payload->second.empty()) {
        output_path = with_extension(output_path, payload->second);
    }
    output_path = context.output_paths.allocate(output_path);
    if (auto written = write_binary_file(output_path, payload->first); !written) {
        add_report_failure(report, label, written.error(), options);
        return;
    }
    add_report_success(report, output_path, label, options);
}

template <class Fn>
std::optional<LoadedDocument> call_with_reason(
    Fn&& fn,
    std::string& last_reason,
    std::string_view label
) {
    std::string reason;
    if (auto doc = fn(reason)) {
        return doc;
    }
    if (!reason.empty()) {
        last_reason = std::string(label) + ": " + reason;
    }
    return std::nullopt;
}

template <class T, class LoadFn, class SummarizeFn>
std::optional<LoadedDocument> try_loader(
    const std::filesystem::path& path,
    std::string& rejection_reason,
    LoadFn&& load,
    SummarizeFn&& summarize
) {
    auto loaded = load(path);
    if (!loaded) {
        rejection_reason = loaded.error();
        return std::nullopt;
    }
    return summarize(path, static_cast<const T&>(*loaded));
}

std::optional<LoadedDocument> load_document_summary_with_order(
    const std::filesystem::path& path,
    const std::vector<std::string>& order,
    std::string& rejection_reason,
    const DecryptionKeys& keys
) {
    if (order.empty()) {
        rejection_reason = "no supported header signature detected";
        return std::nullopt;
    }

    std::vector<std::string> tried;
    for (const auto& type : order) {
        if (std::ranges::find(tried, type) != tried.end()) {
            continue;
        }
        tried.push_back(type);

        if (type == "cpk") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::cpk::Cpk>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::cpk::Cpk::load(source); },
                        modules::cpk::summarize
                    );
                }, rejection_reason, "CPK")) return doc;
        } else if (type == "csb") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::csb::CsbContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::csb::CsbContainer::load(source); },
                        modules::csb::summarize
                    );
                }, rejection_reason, "CSB")) return doc;
        } else if (type == "acb") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::acb::AcbContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::acb::AcbContainer::load(source); },
                        modules::acb::summarize
                    );
                }, rejection_reason, "ACB")) return doc;
        } else if (type == "awb") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::awb::AwbContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::awb::AwbContainer::load(source); },
                        modules::awb::summarize
                    );
                }, rejection_reason, "AWB")) return doc;
        } else if (type == "usm") {
            auto reader = cricodecs::usm::UsmReader{};
            if (auto result = reader.load(path); result) {
                if (keys.has_cri_key) {
                    reader.set_key(keys.cri_key);
                }
                return modules::usm::summarize(path, reader, usm_video_format_probe);
            } else {
                rejection_reason = "USM: " + result.error();
            }
        } else if (type == "sfd") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::sfd::SfdContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::sfd::SfdContainer::load(source); },
                        modules::sfd::summarize
                    );
                }, rejection_reason, "SFD")) return doc;
        } else if (type == "sbt") {
            auto bytes = cricodecs::io::read_file_bytes(path, "SBT load failed");
            if (!bytes) {
                rejection_reason = bytes.error();
                continue;
            }
            auto doc = modules::usm::summarize_sbt_subtitles(path, *bytes, rejection_reason);
            if (rejection_reason.empty()) {
                doc.loader_tag = "sbt";
                return doc;
            }
        } else if (type == "afs") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::afs::AfsContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::afs::AfsContainer::load(source); },
                        modules::afs::summarize
                    );
                }, rejection_reason, "AFS")) return doc;
        } else if (type == "aax") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::aax::AaxContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::aax::AaxContainer::load(source); },
                        modules::aax::summarize
                    );
                }, rejection_reason, "AAX")) return doc;
        } else if (type == "aix") {
            cricodecs::aix::Aix aix;
            if (auto result = aix.load(path); result) {
                return modules::aix::summarize(path, aix);
            } else {
                rejection_reason = "AIX: " + result.error();
            }
        } else if (type == "acx") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::acx::AcxContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::acx::AcxContainer::load(source); },
                        modules::acx::summarize
                    );
                }, rejection_reason, "ACX")) return doc;
        } else if (type == "adx") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::adx::Adx>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::adx::Adx::load(source); },
                        modules::adx::summarize
                    );
                }, rejection_reason, "ADX")) return doc;
        } else if (type == "hca") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::hca::Hca>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::hca::Hca::load(source); },
                        modules::hca::summarize
                    );
                }, rejection_reason, "HCA")) return doc;
        } else if (type == "cvm") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::cvm::CvmContainer>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) {
                            return cricodecs::cvm::CvmContainer::load(source);
                        },
                        modules::cvm::summarize
                    );
                }, rejection_reason, "CVM")) return doc;
        } else if (type == "utf") {
            if (auto doc = call_with_reason([&](std::string& reason) {
                    return try_loader<cricodecs::utf::UtfTable>(
                        path,
                        reason,
                        [](const std::filesystem::path& source) { return cricodecs::utf::UtfTable::load(source); },
                        modules::utf::summarize
                    );
                }, rejection_reason, "UTF")) return doc;
        }
    }

    if (rejection_reason.empty()) {
        rejection_reason = "no supported CRI loader accepted the file";
    }
    return std::nullopt;
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> raw_extract_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    return raw_extract_transform(bytes, keys);
}

std::optional<LoadedDocument> load_document_summary(
    const std::filesystem::path& path,
    std::string& rejection_reason,
    const DecryptionKeys& keys
) {
    rejection_reason.clear();
    if (!std::filesystem::is_regular_file(path)) {
        rejection_reason = "not a regular file";
        return std::nullopt;
    }

    return load_document_summary_with_order(path, sniff_file_format_order(path), rejection_reason, keys);
}

std::optional<LoadedDocument> probe_document_summary(
    const std::filesystem::path& path,
    std::string& rejection_reason
) {
    rejection_reason.clear();
    if (!std::filesystem::is_regular_file(path)) {
        rejection_reason = "not a regular file";
        return std::nullopt;
    }

    const auto order = sniff_file_format_order(path);
    if (order.empty()) {
        rejection_reason = "no supported header signature detected";
        return std::nullopt;
    }

    // These path loaders keep their archive bytes file-backed. Parse their
    // header/directory metadata now so contained names are searchable, then
    // discard the mapped container; entry payloads remain lazy.
    if (supports_file_backed_metadata_index(order.front())) {
        DecryptionKeys no_keys;
        auto doc = load_document_summary_with_order(
            path, {order.front()}, rejection_reason, no_keys);
        if (!doc) {
            return std::nullopt;
        }
        doc->loader_tag = order.front();
        doc->summary_loaded = true;
        return doc;
    }

    auto doc = base_document(path, display_format_for_loader_tag(order.front()));
    doc.loader_tag = order.front();
    doc.summary_loaded = false;
    doc.info.push_back({"Status", "Load on selection"});
    return doc;
}

std::optional<LoadedDocument> materialize_document_summary(
    const LoadedDocument& document,
    std::string& rejection_reason,
    const DecryptionKeys& keys
) {
    if (document.summary_loaded) {
        rejection_reason.clear();
        return document;
    }

    if (!document.loader_tag.empty()) {
        if (auto loaded = load_document_summary_with_order(document.path, {document.loader_tag}, rejection_reason, keys)) {
            loaded->loader_tag = document.loader_tag;
            loaded->summary_loaded = true;
            return loaded;
        }
    }

    auto loaded = load_document_summary(document.path, rejection_reason, keys);
    if (loaded) {
        loaded->summary_loaded = true;
        if (loaded->loader_tag.empty()) {
            loaded->loader_tag = document.loader_tag;
        }
    }
    return loaded;
}

std::optional<LoadedDocument> load_embedded_entry_summary(
    const EntrySummary& entry,
    std::string& rejection_reason,
    const DecryptionKeys& keys
) {
    rejection_reason.clear();
    auto bytes = extract_embedded_entry_payload(entry, keys);
    if (!bytes) {
        rejection_reason = bytes.error();
        return std::nullopt;
    }

    if (bytes->empty()) {
        rejection_reason = "entry is empty";
        return std::nullopt;
    }

    auto doc = summarize_embedded_bytes(entry, *bytes, rejection_reason);
    if (doc) {
        attach_nested_sources(*doc, entry);
    }
    if (!doc && rejection_reason.empty()) {
        rejection_reason = "embedded entry is not a supported preview format";
    }
    return doc;
}

std::expected<std::vector<uint8_t>, std::string> load_document_bytes(
    const LoadedDocument& document
) {
    return read_binary_file(document.path);
}

std::expected<std::vector<uint8_t>, std::string> load_embedded_entry_bytes(
    const EntrySummary& entry,
    const DecryptionKeys& keys
) {
    return extract_embedded_entry_payload(entry, keys);
}

std::expected<AudioPreview, std::string> build_audio_preview(
    const LoadedDocument& document,
    const DecryptionKeys& keys
) {
    return build_direct_audio_preview(document, keys);
}

std::expected<MuxPreview, std::string> build_mux_preview(
    const LoadedDocument& document,
    int audio_choice,
    const DecryptionKeys& keys
) {
    if (!is_mux_document_format(document.format)) {
        return std::unexpected("mux preview is only available for USM/SFD documents");
    }

    std::optional<VideoPreview> video;
    std::vector<const EntrySummary*> audio_entries;
    std::vector<MuxSubtitleChoice> subtitle_choices;
    for (const auto& entry : document.entries) {
        if (!entry.has_source) {
            continue;
        }

        const bool video_candidate = !video && is_mux_video_entry(entry);
        const bool audio_candidate = is_audio_entry(entry);
        const bool subtitle_candidate = is_sbt_entry(entry);
        if (!video_candidate && !audio_candidate && !subtitle_candidate) {
            continue;
        }

        if (audio_candidate) {
            audio_entries.push_back(std::addressof(entry));
        }

        std::optional<std::vector<uint8_t>> bytes;
        if (video_candidate || subtitle_candidate) {
            auto extracted = extract_embedded_entry_payload(entry, keys);
            if (!extracted || extracted->empty()) {
                continue;
            }
            bytes = std::move(*extracted);
        }

        if (video_candidate) {
            video = video_preview_from_bytes(entry, *bytes);
        }

        if (subtitle_candidate) {
            if (auto tracks = cricodecs::usm::sbt_to_srt_tracks(*bytes)) {
                for (const auto& [language_id, srt] : *tracks) {
                    subtitle_choices.push_back({
                        .name = entry.name.empty()
                            ? "SBT subtitles language " + number(language_id)
                            : entry.name + " language " + number(language_id),
                        .detail = "language " + number(language_id),
                        .source_index = entry.source_index,
                        .language_id = language_id,
                        .srt_text = srt,
                    });
                }
            }
        }
    }

    if (!video) {
        return std::unexpected("mux preview could not find a video stream");
    }

    MuxPreview preview;
    preview.video_bytes = std::move(video->video_bytes);
    preview.video_suffix = std::move(video->file_suffix);
    preview.ffmpeg_input_format = std::move(video->ffmpeg_input_format);
    preview.format = std::move(video->format);
    preview.note = document.display_name;
    preview.frame_rate_n = video->frame_rate_n;
    preview.frame_rate_d = video->frame_rate_d;
    preview.duration_ms = video->duration_ms;
    preview.subtitle_choices = std::move(subtitle_choices);
    preview.selected_subtitle = preview.subtitle_choices.empty() ? -1 : 0;
    preview.audio_choices.reserve(audio_entries.size());
    for (size_t i = 0; i < audio_entries.size(); ++i) {
        const auto& audio = *audio_entries[i];
        preview.audio_choices.push_back({
            audio.name,
            audio.type,
            audio.detail,
            static_cast<uint32_t>(i)
        });
    }

    if (audio_entries.empty() || audio_choice < 0) {
        preview.selected_audio = -1;
        return preview;
    }

    const auto selected = static_cast<size_t>(std::min<int>(
        audio_choice,
        static_cast<int>(audio_entries.size() - 1)
    ));
    preview.selected_audio = static_cast<int>(selected);
    const auto& audio_entry = *audio_entries[selected];
    auto audio_bytes = extract_embedded_entry_payload(audio_entry, keys, EmbeddedPayloadPurpose::Playback);
    if (!audio_bytes) {
        return std::unexpected("audio stream extract failed: " + audio_bytes.error());
    }
    auto audio = audio_preview_from_entry_bytes(audio_entry, *audio_bytes, keys);
    if (!audio) {
        return std::unexpected("audio stream preview failed: " + audio.error());
    }
    preview.audio_wav_bytes = std::move(audio->wav_bytes);
    if (preview.audio_wav_bytes.empty() && !audio->playable_path.empty()) {
        std::ifstream input(audio->playable_path, std::ios::binary);
        if (input) {
            preview.audio_wav_bytes.assign(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            );
        }
    }
    if (preview.audio_wav_bytes.empty()) {
        return std::unexpected("audio stream did not produce WAV data");
    }
    preview.audio_label = audio_entry.name;
    return preview;
}

EmbeddedPreview load_embedded_entry_preview(const EntrySummary& entry, const DecryptionKeys& keys) {
    EmbeddedPreview preview;
    auto bytes = extract_embedded_entry_payload(entry, keys, EmbeddedPayloadPurpose::Playback);
    if (!bytes) {
        preview.message = bytes.error();
        return preview;
    }
    if (bytes->empty()) {
        preview.message = "entry is empty";
        return preview;
    }

    constexpr size_t max_hex_preview_bytes = 256u * 1024u;
    preview.hex_dump = hex_dump(*bytes, max_hex_preview_bytes, preview.hex_truncated);
    preview.raw_total_size = bytes->size();
    const auto retain_bounded_raw_preview = [&] {
        preview.raw_preview_bytes.assign(
            bytes->begin(),
            bytes->begin() + static_cast<std::ptrdiff_t>(std::min(bytes->size(), max_hex_preview_bytes))
        );
    };

    std::string reason;
    if (auto doc = summarize_embedded_bytes(entry, *bytes, reason)) {
        attach_nested_sources(*doc, entry);
        if (is_direct_audio_format(doc->format)) {
            const auto preview_keys = preview_keys_for_entry(entry, keys);
            if (auto audio = audio_preview_from_bytes(*doc, *bytes, preview_keys); audio) {
                preview.audio = std::move(*audio);
            } else {
                preview.message = audio.error();
            }
        }
        if (doc->format.find("SBT") != std::string::npos && doc->format.find("subtitle") != std::string::npos) {
            preview.raw_preview_bytes = std::move(*bytes);
            preview.hex_truncated = false;
        } else {
            retain_bounded_raw_preview();
        }
        preview.document = std::move(*doc);
        return preview;
    }
    if (cricodecs::awb::probe_entry_codec(*bytes) != cricodecs::awb::EntryCodec::Unknown) {
        retain_bounded_raw_preview();
        if (auto audio = audio_preview_from_entry_bytes(entry, *bytes, keys)) {
            preview.audio = std::move(*audio);
        } else {
            preview.message = audio.error();
        }
        return preview;
    }
    if (auto video = video_preview_from_bytes(entry, *bytes)) {
        retain_bounded_raw_preview();
        preview.video = std::move(*video);
        return preview;
    }

    if (entry.source_format == "USM" && !entry.has_nested_source) {
        cricodecs::usm::UsmReader usm;
        if (auto loaded = usm.load(entry.source_path); loaded && entry.source_index < usm.streams().size()) {
            if (keys.has_cri_key) {
                usm.set_key(keys.cri_key);
            }
            const auto id = usm.streams()[entry.source_index].id();
            if (
                id.stream_id == cricodecs::usm::UsmChunkType::SFV ||
                id.stream_id == cricodecs::usm::UsmChunkType::ALP ||
                id.stream_id == cricodecs::usm::UsmChunkType::SFA ||
                id.stream_id == cricodecs::usm::UsmChunkType::AHX
            ) {
                preview.message = "stream payload is not directly decodable; USM masking state is unknown until key-recovery or codec validation provides evidence";
            }
        }
    }

    constexpr size_t max_image_preview_bytes = 32u * 1024u * 1024u;
    if (bytes->size() <= max_image_preview_bytes && likely_image_entry(entry.name, *bytes)) {
        preview.preview_bytes = *bytes;
    }

    if (preview.message.empty()) {
        preview.message = reason.empty() ? "unknown preview format; showing hex" : reason;
    }
    retain_bounded_raw_preview();
    return preview;
}

ExtractionReport extract_targets(
    const std::vector<ExtractionTarget>& targets,
    const std::filesystem::path& output_dir,
    ExtractionMode mode,
    const DecryptionKeys& keys,
    const ExtractionOptions& options
) {
    ExtractionReport report;
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        report.total = targets.size();
        report.failed = targets.size();
        auto message = "Failed to create extraction directory: " + filesystem_error.message();
        report.messages.push_back(message);
        if (options.event_callback) {
            options.event_callback(ExtractionEvent{
                .processed_delta = targets.size(),
                .failed_delta = targets.size(),
                .message = std::move(message),
            });
        }
        return report;
    }

    ExtractionContext context;
    for (const auto& target : targets) {
        switch (target.kind) {
        case ExtractionTarget::Kind::Document:
            extract_document_into_report(target.document, output_dir, mode, keys, options, context, report);
            break;
        case ExtractionTarget::Kind::Entry:
            extract_entry_into_report(target.entry, output_dir, mode, keys, options, context, report);
            break;
        }
        if (options.event_callback) {
            options.event_callback(ExtractionEvent{.processed_delta = 1});
        }
    }

    return report;
}

} // namespace cristudio
