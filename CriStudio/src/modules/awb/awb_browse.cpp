#include "modules/awb/awb_browse.hpp"

#include "shared/document_helpers.hpp"

#include <cstdint>
#include <utility>

namespace cristudio::modules::awb {
namespace {

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

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::awb::AwbContainer& awb) {
    auto doc = base_document(path, "AWB audio bank");
    doc.info.push_back({"Entries", number(awb.file_count())});
    doc.info.push_back({"Version", number(awb.version())});
    doc.info.push_back({"Alignment", number(awb.alignment())});
    doc.info.push_back({"ID size", number(awb.id_size())});
    doc.info.push_back({"Offset size", number(awb.offset_size())});

    doc.entries.reserve(awb.entries().size());
    for (size_t i = 0; i < awb.entries().size(); ++i) {
        const auto& entry = awb.entries()[i];
        const auto codec = awb.entry_codec(static_cast<uint32_t>(i));
        const auto extension = codec
            ? cricodecs::awb::entry_codec_extension(*codec)
            : std::string_view{".bin"};
        auto summary = sourced_entry({
            "wave_" + number(entry.wave_id) + std::string(extension),
            codec ? std::string(cricodecs::awb::entry_codec_name(*codec)) : "audio",
            byte_count(entry.size),
            number(entry.offset),
            "index " + number(i)
        }, path, "AWB", static_cast<uint32_t>(i));
        summary.hca_subkey = awb.subkey();
        doc.entries.push_back(std::move(summary));
    }
    return doc;
}

} // namespace cristudio::modules::awb
