#include "modules/sfd/sfd_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace cristudio::modules::sfd {
namespace {

std::string hex_byte(uint8_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(value);
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

std::string stream_type(const cricodecs::sfd::SfdStream& stream) {
    switch (stream.type) {
    case cricodecs::sfd::SfdStreamType::audio:
        return std::string("audio") + cricodecs::sfd::stream_extension(stream.audio_type);
    case cricodecs::sfd::SfdStreamType::video:
        return stream.video_type == cricodecs::sfd::SfdVideoType::mpeg1 ? "MPEG-1 video"
             : stream.video_type == cricodecs::sfd::SfdVideoType::mpeg2 ? "MPEG-2 video"
             : "video";
    case cricodecs::sfd::SfdStreamType::private_data:
        return "private";
    }
    return "stream";
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::sfd::SfdContainer& sfd) {
    auto doc = base_document(path, "SFD/SofDec stream");
    doc.info.push_back({"Streams", number(sfd.stream_count())});
    if (const auto& header = sfd.header_summary()) {
        doc.info.push_back({"Header", header->header_label});
        doc.info.push_back({"Pack size", number(header->pack_size)});
        doc.info.push_back({"Audio streams", number(header->audio_count)});
        doc.info.push_back({"Video streams", number(header->video_count)});
        doc.info.push_back({"Builder", header->builder_version});
    }

    doc.entries.reserve(sfd.streams().size());
    for (const auto& stream : sfd.streams()) {
        doc.entries.push_back(sourced_entry({
            archive_display_path(stream.suggested_path().generic_string()),
            stream_type(stream),
            byte_count(stream.extracted_size),
            "stream " + hex_byte(stream.stream_id),
            "packets " + number(stream.packet_count) + ", stream id " + hex_byte(stream.stream_id)
        }, path, "SFD", stream.index));
    }
    return doc;
}

} // namespace cristudio::modules::sfd
