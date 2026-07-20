#include "modules/csb/csb_browse.hpp"

#include "path_text.hpp"
#include "shared/document_helpers.hpp"

#include <utility>

namespace cristudio::modules::csb {
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

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::csb::CsbContainer& csb) {
    auto doc = base_document(path, "CSB cue archive");
    doc.info.push_back({"Name", std::string(csb.name())});
    doc.info.push_back({"Sections", number(csb.section_count())});
    doc.info.push_back({"Elements", number(csb.element_count())});
    doc.info.push_back({"Streams", number(csb.stream_count())});

    doc.entries.reserve(csb.stream_count());
    for (uint32_t i = 0; i < csb.stream_count(); ++i) {
        const auto& stream = csb.stream(i);
        doc.entries.push_back(sourced_entry({
            archive_display_path(stream.suggested_path().generic_string()),
            stream.wrapper_table_name.empty() ? std::string(cricodecs::csb::stream_file_extension(stream.format))
                                              : stream.wrapper_table_name,
            byte_count(stream.wrapper_size),
            indexed_label("stream", i),
            number(stream.channels) + " ch, " + number(stream.sample_rate) + " Hz"
        }, path, "CSB", i));
    }
    return doc;
}

} // namespace cristudio::modules::csb
