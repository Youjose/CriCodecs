#include "shared/i18n.hpp"
#include "modules/cpk/cpk_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace cristudio::modules::cpk {
namespace {

std::string preset_name(cricodecs::cpk::CpkPreset preset) {
    switch (preset) {
    case cricodecs::cpk::CpkPreset::Custom: return "Custom";
    case cricodecs::cpk::CpkPreset::Id: return "ID";
    case cricodecs::cpk::CpkPreset::IdGroup: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "ID + group");
    case cricodecs::cpk::CpkPreset::Filename: return "Filename";
    case cricodecs::cpk::CpkPreset::FilenameGroup: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Filename + group");
    case cricodecs::cpk::CpkPreset::FilenameId: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Filename + ID");
    case cricodecs::cpk::CpkPreset::FilenameIdGroup: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Filename + ID + group");
    }
    return "Unknown";
}

std::string mode_name(cricodecs::cpk::CpkMode mode) {
    switch (mode) {
    case cricodecs::cpk::CpkMode::Mode0: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Mode 0 / ITOC");
    case cricodecs::cpk::CpkMode::Mode1: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Mode 1 / TOC");
    case cricodecs::cpk::CpkMode::Mode2: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Mode 2 / TOC + ITOC");
    case cricodecs::cpk::CpkMode::Mode3: return cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Mode 3 / TOC + ITOC + GTOC");
    }
    return "Unknown";
}

std::optional<uint64_t> header_u64(const cricodecs::cpk::Cpk& cpk, std::string_view field) {
    const auto& header = cpk.cpk_header();
    const auto column = header.find_column(field);
    if (column < 0) {
        return std::nullopt;
    }
    auto value = header.get_value(0, static_cast<uint32_t>(column));
    if (!value) {
        return std::nullopt;
    }
    return std::visit([](const auto& item) -> std::optional<uint64_t> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_integral_v<T>) {
            if constexpr (std::is_signed_v<T>) {
                if (item < 0) {
                    return std::nullopt;
                }
            }
            return static_cast<uint64_t>(item);
        } else {
            return std::nullopt;
        }
    }, *value);
}

void add_chunk_location(LoadedDocument& doc, const cricodecs::cpk::Cpk& cpk, std::string_view name) {
    const auto offset = header_u64(cpk, std::string(name) + "Offset");
    const auto size = header_u64(cpk, std::string(name) + "Size");
    if (!offset || !size || *offset == 0 || *size == 0) {
        return;
    }
    const auto offset_id = std::string(name) + " offset";
    const auto size_id = std::string(name) + " size";
    doc.info.push_back({
        std::string(name) + cristudio::i18n::translate_utf8("Cpk.CpkBrowse", " offset"),
        number(*offset),
        offset_id
    });
    doc.info.push_back({
        std::string(name) + cristudio::i18n::translate_utf8("Cpk.CpkBrowse", " size"),
        number(*size),
        size_id
    });
}

EntrySummary sourced_entry(
    EntrySummary entry,
    const std::filesystem::path& source_path,
    std::string source_format,
    uint32_t source_index
) {
    entry.source_path = source_path;
    entry.source_format = std::move(source_format);
    entry.source_index = source_index;
    entry.has_source = true;
    return entry;
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::cpk::Cpk& cpk) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "CPK archive"));
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Entries"), number(cpk.file_count())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Mode"), mode_name(cpk.mode())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Preset"), preset_name(cpk.preset())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Declared preset"), cpk.has_declared_preset() ? preset_name(cpk.declared_preset()) : "-"});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Alignment"), number(cpk.alignment())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Content offset"), number(cpk.content_offset())});
    doc.info.push_back({"TOC", bool_text(cpk.has_toc())});
    doc.info.push_back({"ITOC", bool_text(cpk.has_itoc())});
    doc.info.push_back({"GTOC", bool_text(cpk.has_gtoc())});
    doc.info.push_back({"ETOC", bool_text(cpk.has_etoc())});
    add_chunk_location(doc, cpk, "Toc");
    add_chunk_location(doc, cpk, "Itoc");
    add_chunk_location(doc, cpk, "Gtoc");
    add_chunk_location(doc, cpk, "Etoc");
    const auto& options = cpk.options();
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "CRC option"), bool_text(options.enable_crc)});
    doc.info.push_back({"TVER", options.tver.empty() ? "-" : options.tver});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "Comment"), options.comment});
    doc.info.push_back({cristudio::i18n::translate_utf8("Cpk.CpkBrowse", "ETOC LocalDir"), options.etoc_local_dir.empty() ? "-" : options.etoc_local_dir});

    doc.entries.reserve(cpk.files().size());
    for (uint32_t i = 0; i < cpk.files().size(); ++i) {
        const auto& file = cpk.files()[i];
        auto type = file.is_compressed ? std::string("compressed") : std::string("data");
        auto entry = sourced_entry({
            archive_display_path(file.full_path().generic_string()),
            type,
            byte_count(file.extract_size != 0 ? file.extract_size : file.file_size),
            number(file.file_offset),
            "id " + number(file.id) + cristudio::i18n::translate_utf8("Cpk.CpkBrowse", ", toc ") + number(file.toc_index)
        }, path, "CPK", i);
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::cpk
