#include "modules/acx/acx_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace cristudio::modules::acx {
namespace {

std::string hex_u64(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
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

std::string entry_type(const cricodecs::acx::AcxEntry& entry) {
    switch (entry.type) {
    case cricodecs::acx::AcxEntryType::adx:
        return "ADX";
    case cricodecs::acx::AcxEntryType::ogg:
        return "Ogg Vorbis";
    case cricodecs::acx::AcxEntryType::unknown:
        break;
    }
    return "data";
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::acx::AcxContainer& acx) {
    auto doc = base_document(path, "ACX audio archive");
    doc.info.push_back({"Entries", number(acx.entry_count())});
    doc.info.push_back({"Table size", number(acx.table_size())});
    doc.info.push_back({"First payload offset", acx.first_payload_offset() ? hex_u64(*acx.first_payload_offset()) : "-"});
    doc.info.push_back({"Payload end offset", acx.payload_end_offset() ? hex_u64(*acx.payload_end_offset()) : "-"});
    doc.info.push_back({"ADX entries", number(acx.type_count(cricodecs::acx::AcxEntryType::adx))});
    doc.info.push_back({"Ogg entries", number(acx.type_count(cricodecs::acx::AcxEntryType::ogg))});
    doc.info.push_back({"Unknown entries", number(acx.type_count(cricodecs::acx::AcxEntryType::unknown))});

    doc.entries.reserve(acx.entries().size());
    for (const auto& entry : acx.entries()) {
        doc.entries.push_back(sourced_entry({
            archive_display_path(entry.suggested_path().generic_string()),
            entry_type(entry),
            byte_count(entry.size),
            number(entry.offset),
            "index " + number(entry.index) + ", table row " + hex_u64(0x08ull + static_cast<uint64_t>(entry.index) * 0x08ull)
        }, path, "ACX", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::acx
