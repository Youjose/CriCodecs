#include "modules/cvm/cvm_browse.hpp"

#include "shared/document_helpers.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace cristudio::modules::cvm {
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

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::cvm::CvmContainer& cvm) {
    auto doc = base_document(path, "CVM/ROFS image");
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& primary = cvm.primary_volume();
    doc.info.push_back({"Disc name", cvm.disc_name()});
    doc.info.push_back({"Recording date", cvm.recording_date_text()});
    doc.info.push_back({"Media", cvm.media()});
    doc.info.push_back({"Scrambled", bool_text(cvm.is_scrambled())});
    doc.info.push_back({"Entries", number(cvm.entry_count())});
    doc.info.push_back({"Accessible", bool_text(cvm.has_accessible_contents())});
    if (cvm.is_scrambled() && !cvm.has_accessible_contents()) {
        doc.info.push_back({"Key", "required for TOC"});
    }
    doc.info.push_back({"Flags", hex_u64(header.flags)});
    doc.info.push_back({"Filesystem", header.filesystem_id});
    doc.info.push_back({"Maker", header.maker_id});
    doc.info.push_back({"Zone sector", number(zone.zone_sector)});
    doc.info.push_back({"Data sector", number(zone.data_sector)});
    doc.info.push_back({"ISO sector", number(zone.iso_sector)});
    doc.info.push_back({"ISO offset", number(cvm.embedded_iso_offset())});
    doc.info.push_back({"ISO size", byte_count(cvm.embedded_iso_size())});
    doc.info.push_back({"ISO sectors", number(cvm.embedded_iso_sector_count())});
    doc.info.push_back({"System ID", primary.system_identifier});
    doc.info.push_back({"Volume ID", primary.volume_identifier});
    doc.info.push_back({"Volume set", primary.volume_set_identifier});
    doc.info.push_back({"Logical block", number(primary.logical_block_size)});

    doc.entries.reserve(cvm.entries().size());
    for (const auto& entry : cvm.entries()) {
        doc.entries.push_back(sourced_entry({
            archive_display_path(entry.path.generic_string()),
            "file",
            byte_count(entry.size),
            "sector " + number(entry.extent_sector),
            "index " + number(entry.index)
        }, path, "CVM", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::cvm
