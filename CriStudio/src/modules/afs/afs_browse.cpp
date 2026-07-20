#include "modules/afs/afs_browse.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <span>
#include <utility>

namespace cristudio::modules::afs {
namespace {

std::string hex_u64(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

std::string hex_bytes(std::span<const uint8_t> bytes) {
    std::ostringstream out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(bytes[i]);
    }
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

std::string timestamp_text(const cricodecs::afs::AfsDirectoryTimestamp& timestamp) {
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << timestamp.year << '-'
        << std::setw(2) << std::setfill('0') << timestamp.month << '-'
        << std::setw(2) << std::setfill('0') << timestamp.day << ' '
        << std::setw(2) << std::setfill('0') << timestamp.hour << ':'
        << std::setw(2) << std::setfill('0') << timestamp.minute << ':'
        << std::setw(2) << std::setfill('0') << timestamp.second;
    return out.str();
}

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::afs::AfsContainer& afs) {
    auto doc = base_document(path, "AFS archive");
    doc.info.push_back({"Entries", number(afs.entry_count())});
    doc.info.push_back({"Present entries", number(afs.present_entry_count())});
    doc.info.push_back({"Alignment", number(afs.alignment())});
    doc.info.push_back({"Directory table", bool_text(afs.has_directory_table())});
    doc.info.push_back({"Directory table on build", bool_text(afs.directory_table_enabled())});
    doc.info.push_back({"Directory offset", afs.directory_table_offset() ? hex_u64(*afs.directory_table_offset()) : "-"});
    doc.info.push_back({"Directory size", afs.directory_table_size() ? byte_count(*afs.directory_table_size()) : "-"});
    doc.info.push_back({"First payload offset", afs.first_payload_offset() ? hex_u64(*afs.first_payload_offset()) : "-"});

    doc.entries.reserve(afs.entries().size());
    for (const auto& entry : afs.entries()) {
        if (!entry.present) {
            EntrySummary reserved;
            reserved.name = "file_id " + number(entry.index);
            reserved.type = "reserved";
            reserved.detail = "empty file ID slot";
            reserved.cells = {reserved.name, reserved.type, {}, {}, reserved.detail};
            doc.entries.push_back(std::move(reserved));
            continue;
        }
        auto detail = "id " + number(entry.index);
        if (auto timestamp = entry.directory_timestamp()) {
            detail += ", timestamp " + timestamp_text(*timestamp);
        }
        if (!entry.header_source_name.value_or("").empty()) {
            detail += ", header " + *entry.header_source_name;
        }
        if (std::ranges::any_of(entry.directory_metadata, [](uint8_t value) { return value != 0; })) {
            detail += ", metadata " + hex_bytes(entry.directory_metadata);
        }
        doc.entries.push_back(sourced_entry({
            archive_display_path(entry.suggested_path().generic_string()),
            cricodecs::afs::entry_extension(entry.type),
            byte_count(entry.size),
            number(entry.offset),
            detail
        }, path, "AFS", entry.index));
    }
    return doc;
}

} // namespace cristudio::modules::afs
