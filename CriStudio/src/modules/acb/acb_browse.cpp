#include "shared/i18n.hpp"
#include "modules/acb/acb_browse.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace cristudio::modules::acb {
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

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::acb::AcbContainer& acb) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Acb.AcbBrowse", "ACB cue sheet"));
    doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Name"), std::string(acb.name())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Waveforms"), number(acb.waveform_count())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Resolved names"), number(static_cast<uint64_t>(acb.wave_names().size()))});
    doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Embedded AWB"), bool_text(acb.has_embedded_awb())});
    if (auto embedded = acb.embedded_awb()) {
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Embedded AWB bytes"), number(embedded->size())});
    }
    if (auto awb_path = acb.companion_awb_path()) {
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Companion AWB"), generic_path(*awb_path)});
    }
    uint16_t associated_awb_subkey = 0;
    if (auto awb = acb.load_awb()) {
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Associated AWB files"), number(awb->file_count())});
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Associated AWB alignment"), number(awb->alignment())});
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Associated AWB subkey"), number(awb->subkey())});
        associated_awb_subkey = awb->subkey();
    } else {
        doc.info.push_back({cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Associated AWB"), awb.error()});
    }
    doc.entry_columns = {cristudio::i18n::translate_utf8("Acb.AcbBrowse", "Name"), "Codec"};
    doc.entry_column_types = {"name", "type"};

    doc.entries.reserve(acb.waveform_count());
    for (uint32_t i = 0; i < acb.waveform_count(); ++i) {
        const auto& wave = acb.waveform(i);
        auto codec = std::string(cricodecs::acb::encode_type_extension(wave.encode_type));
        if (!codec.empty() && codec.front() == '.') {
            codec.erase(codec.begin());
        }
        if (!codec.empty()) {
            std::ranges::transform(codec, codec.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            codec += " audio";
        } else {
            codec = "audio";
        }
        auto entry = sourced_entry({
            acb.waveform_filename(i),
            codec,
            indexed_label("waveform", i),
            "memory " + number(wave.memory_awb_id) + cristudio::i18n::translate_utf8("Acb.AcbBrowse", ", stream ") + number(wave.stream_awb_id),
            bool_text(wave.loop_flag)
        }, path, "ACB", i);
        entry.hca_subkey = associated_awb_subkey;
        entry.cells = {
            entry.name,
            entry.type
        };
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::acb
