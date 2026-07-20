#include "modules/aix/aix_browse.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <utility>

namespace cristudio::modules::aix {
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

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::aix::Aix& aix) {
    auto doc = base_document(path, "AIX audio container");
    doc.info.push_back({"Segments", number(aix.segments().size())});
    doc.info.push_back({"Layers", number(aix.layers().size())});
    doc.info.push_back({"Samples", number(aix.total_sample_count())});
    doc.info.push_back({"Inferred loop", bool_text(aix.inferred_loop().has_value())});

    const auto layer_count = std::max<size_t>(aix.layers().size(), 1);
    for (size_t segment_index = 0; segment_index < aix.segments().size(); ++segment_index) {
        const auto& segment = aix.segments()[segment_index];
        if (layer_count <= 1) {
            const auto detail = aix.layers().empty()
                ? "samples " + number(segment.sample_count) + ", " + number(segment.sample_rate) + " Hz"
                : "samples " + number(segment.sample_count) + ", " +
                    number(aix.layers().front().sample_rate == 0 ? static_cast<uint32_t>(segment.sample_rate) : aix.layers().front().sample_rate) +
                    " Hz, " + number(aix.layers().front().channel_count) + " ch";
            doc.entries.push_back(sourced_entry({
                "segment " + number(segment_index),
                "ADX",
                byte_count(segment.size),
                number(segment.offset),
                detail
            }, path, "AIX", static_cast<uint32_t>(segment_index * layer_count)));
            continue;
        }

        for (size_t layer_index = 0; layer_index < aix.layers().size(); ++layer_index) {
            const auto& layer = aix.layers()[layer_index];
            doc.entries.push_back(sourced_entry({
                "segment " + number(segment_index) + "/layer " + number(layer_index),
                "ADX",
                byte_count(segment.size),
                number(segment.offset),
                "samples " + number(segment.sample_count) + ", " +
                    number(layer.sample_rate == 0 ? static_cast<uint32_t>(segment.sample_rate) : layer.sample_rate) +
                    " Hz, " + number(layer.channel_count) + " ch"
            }, path, "AIX", static_cast<uint32_t>(segment_index * layer_count + layer_index)));
        }
    }
    return doc;
}

} // namespace cristudio::modules::aix
