#include "shared/i18n.hpp"
#include "modules/adx/adx_browse.hpp"

#include "shared/document_helpers.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace cristudio::modules::adx {
namespace {

std::string hex_u64(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::adx::Adx& adx) {
    auto doc = base_document(path, adx.is_ahx() ? cristudio::i18n::translate_utf8("Adx.AdxBrowse", "AHX audio") : cristudio::i18n::translate_utf8("Adx.AdxBrowse", "ADX audio"));
    const auto& header = adx.header();
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Signature"), hex_u64(header.signature)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Data offset"), number(header.data_offset)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Encoding mode"), number(header.encoding_mode)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Block size"), number(header.block_size)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Bit depth"), number(header.bit_depth)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Channels"), number(header.channels)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Sample rate"), number(header.sample_rate)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Samples"), number(header.sample_count)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Highpass"), number(header.highpass_freq)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Version"), number(header.version)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Flags"), hex_u64(header.flags)});
    doc.info.push_back({
        cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Encrypted"),
        bool_text(adx.is_encrypted()),
        "Encrypted"
    });
    doc.info.push_back({
        cristudio::i18n::translate_utf8("Adx.AdxBrowse", "AHX routed"),
        bool_text(adx.is_ahx()),
        "AHX routed"
    });
    doc.info.push_back({cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Loop count"), number(adx.loops().size())});
    for (const auto& loop : adx.loops()) {
        doc.info.push_back({
            indexed_label(cristudio::i18n::translate_utf8("Adx.AdxBrowse", "Loop"), loop.index),
            cristudio::i18n::translate_utf8("Adx.AdxBrowse", "type ") + number(loop.type) +
                cristudio::i18n::translate_utf8("Adx.AdxBrowse", ", samples ") + number(loop.start_sample) + "-" + number(loop.end_sample) +
                cristudio::i18n::translate_utf8("Adx.AdxBrowse", ", bytes ") + number(loop.start_byte) + "-" + number(loop.end_byte)
        });
    }
    return doc;
}

} // namespace cristudio::modules::adx
