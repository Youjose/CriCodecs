#include "modules/aax/aax_browse.hpp"

#include "shared/document_helpers.hpp"

#include <optional>
#include <variant>

namespace cristudio::modules::aax {
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

uint64_t aax_data_base(const cricodecs::aax::AaxContainer& aax) {
    return aax.table().data_offset();
}

std::optional<uint64_t> segment_offset(const cricodecs::aax::AaxContainer& aax, uint32_t row) {
    const auto data_col = aax.table().find_column("data");
    if (data_col < 0) {
        return std::nullopt;
    }
    auto value = aax.table().get_value(row, static_cast<uint32_t>(data_col));
    if (!value || !std::holds_alternative<cricodecs::utf::DataRef>(*value)) {
        return std::nullopt;
    }
    return aax_data_base(aax) + std::get<cricodecs::utf::DataRef>(*value).offset;
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::aax::AaxContainer& aax) {
    auto doc = base_document(path, "AAX audio wrapper");
    doc.info.push_back({"Name", std::string(aax.name())});
    doc.info.push_back({"Segments", number(aax.segment_count())});
    doc.info.push_back({"Channels", number(aax.channels())});
    doc.info.push_back({"Sample rate", number(aax.sample_rate())});
    doc.info.push_back({"Samples", number(aax.sample_count())});
    doc.info.push_back({"Loop segments", bool_text(aax.has_loop_segments())});
    doc.entry_columns = {"Segment", "Codec", "Bytes", "Samples", "Loop"};
    doc.entry_column_types = {"name", "type", "size", "u32", "bool"};

    doc.entries.reserve(aax.segments().size());
    for (const auto& segment : aax.segments()) {
        auto entry = sourced_entry({
            "segment " + number(segment.row_index),
            "ADX",
            byte_count(segment.data_size),
            {},
            bool_text(segment.loop_segment)
        }, path, "AAX", segment.row_index);
        if (auto offset = segment_offset(aax, segment.row_index)) {
            entry.offset = number(*offset);
        }
        entry.cells = {
            entry.name,
            entry.type,
            entry.size,
            number(segment.sample_count),
            entry.detail
        };
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::aax
