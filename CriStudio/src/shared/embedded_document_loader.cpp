#include "shared/embedded_document_loader.hpp"

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
#include "modules/wav/wav_browse.hpp"
#include "path_text.hpp"
#include "shared/document_helpers.hpp"
#include "shared/document_sniffer.hpp"
#include "shared/video_probe.hpp"

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
#include "sfd_container.hpp"
#include "usm_container.hpp"
#include "utf_table.hpp"
#include "wav_container.hpp"

#include <algorithm>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

EntrySummary nested_sourced_entry(EntrySummary entry, const EntrySummary& outer_entry) {
    auto nested_format = std::move(entry.source_format);
    const auto nested_index = entry.source_index;
    entry.source_path = outer_entry.source_path;
    entry.source_format = outer_entry.source_format;
    entry.source_index = outer_entry.source_index;
    entry.has_source = outer_entry.has_source;
    entry.nested_source_format = std::move(nested_format);
    entry.nested_source_index = nested_index;
    entry.has_nested_source = true;
    return entry;
}


void fix_embedded_source_info(LoadedDocument& doc, const EntrySummary& entry, uint64_t byte_size) {
    doc.display_name = entry.name;
    doc.file_size = byte_size;
    doc.info.insert(doc.info.begin(), {"Archive entry", entry.name});
    doc.info.insert(doc.info.begin() + 1, {"Source archive", generic_path(entry.source_path)});
    for (auto& row : doc.info) {
        if (row.name == "Size") {
            row.value = byte_count(byte_size);
            break;
        }
    }
}

template <class Summarizer, class Value>
std::optional<LoadedDocument> summarize_embedded(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    std::expected<Value, std::string> loaded,
    Summarizer&& summarizer,
    std::string& rejection_reason
) {
    if (!loaded) {
        rejection_reason = loaded.error();
        return std::nullopt;
    }
    auto doc = summarizer(path_from_utf8_lossy(entry.name), *loaded);
    fix_embedded_source_info(doc, entry, bytes.size());
    return doc;
}

} // namespace

void finalize_embedded_document_summary(
    LoadedDocument& doc,
    const EntrySummary& entry,
    uint64_t byte_size
) {
    fix_embedded_source_info(doc, entry, byte_size);
}

void attach_nested_sources(LoadedDocument& doc, const EntrySummary& outer_entry) {
    for (auto& entry : doc.entries) {
        if (entry.has_source) {
            entry = nested_sourced_entry(std::move(entry), outer_entry);
        } else if (entry.has_cell_sources) {
            auto nested_format = std::move(entry.source_format);
            entry.source_path = outer_entry.source_path;
            entry.source_format = outer_entry.source_format;
            entry.source_index = outer_entry.source_index;
            entry.has_source = false;
            entry.nested_source_format = std::move(nested_format);
            entry.nested_source_index = 0;
            entry.has_nested_source = true;
        }
        for (auto& field : entry.inspector_entries) {
            if (field.has_source) {
                field = nested_sourced_entry(std::move(field), outer_entry);
            }
        }
    }
}

std::optional<LoadedDocument> summarize_embedded_bytes(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes,
    std::string& rejection_reason
) {
    auto order = sniff_embedded_format_order(bytes, entry.name, entry.type, entry.source_format, entry.nested_source_format);

    if (order.empty()) {
        rejection_reason = "no embedded supported header signature detected";
        return std::nullopt;
    }

    std::vector<std::string> tried;
    for (const auto& type : order) {
        if (std::ranges::find(tried, type) != tried.end()) {
            continue;
        }
        tried.push_back(type);

        if (type == "sbt") {
            auto doc = modules::usm::summarize_sbt_subtitles(path_from_utf8_lossy(entry.name), bytes, rejection_reason);
            if (rejection_reason.empty()) {
                fix_embedded_source_info(doc, entry, bytes.size());
                return doc;
            }
        } else if (type == "hca") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::hca::Hca::load(bytes), modules::hca::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "adx") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::adx::Adx::load(bytes), modules::adx::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "wav") {
            cricodecs::wav::WavContainer wav;
            if (auto result = wav.load(bytes); result) {
                auto doc = modules::wav::summarize(path_from_utf8_lossy(entry.name), wav);
                fix_embedded_source_info(doc, entry, bytes.size());
                return doc;
            } else {
                rejection_reason = result.error();
            }
        } else if (type == "usm") {
            cricodecs::usm::UsmReader usm;
            if (auto result = usm.load(bytes); result) {
                auto doc = modules::usm::summarize(path_from_utf8_lossy(entry.name), usm, usm_video_format_probe);
                fix_embedded_source_info(doc, entry, bytes.size());
                return doc;
            } else {
                rejection_reason = result.error();
            }
        } else if (type == "sfd") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::sfd::SfdContainer::load(bytes), modules::sfd::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "aax") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::aax::AaxContainer::load(bytes), modules::aax::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "aix") {
            cricodecs::aix::Aix aix;
            if (auto result = aix.load(bytes); result) {
                auto doc = modules::aix::summarize(path_from_utf8_lossy(entry.name), aix);
                fix_embedded_source_info(doc, entry, bytes.size());
                return doc;
            } else {
                rejection_reason = result.error();
            }
        } else if (type == "acx") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::acx::AcxContainer::load(bytes), modules::acx::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "awb") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::awb::AwbContainer::load(bytes), modules::awb::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "afs") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::afs::AfsContainer::load(bytes), modules::afs::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "csb") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::csb::CsbContainer::load(bytes), modules::csb::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "acb") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::acb::AcbContainer::load(bytes), modules::acb::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "cpk") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::cpk::Cpk::load(bytes), modules::cpk::summarize, rejection_reason)) {
                return doc;
            }
        } else if (type == "utf") {
            if (auto doc = summarize_embedded(entry, bytes, cricodecs::utf::UtfTable::load(bytes), modules::utf::summarize, rejection_reason)) {
                return doc;
            }
        }
    }

    return std::nullopt;
}


} // namespace cristudio
