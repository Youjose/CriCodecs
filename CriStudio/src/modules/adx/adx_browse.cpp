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
    auto doc = base_document(path, adx.is_ahx() ? "AHX audio" : "ADX audio");
    const auto& header = adx.header();
    doc.info.push_back({"Signature", hex_u64(header.signature)});
    doc.info.push_back({"Data offset", number(header.data_offset)});
    doc.info.push_back({"Encoding mode", number(header.encoding_mode)});
    doc.info.push_back({"Block size", number(header.block_size)});
    doc.info.push_back({"Bit depth", number(header.bit_depth)});
    doc.info.push_back({"Channels", number(header.channels)});
    doc.info.push_back({"Sample rate", number(header.sample_rate)});
    doc.info.push_back({"Samples", number(header.sample_count)});
    doc.info.push_back({"Highpass", number(header.highpass_freq)});
    doc.info.push_back({"Version", number(header.version)});
    doc.info.push_back({"Flags", hex_u64(header.flags)});
    doc.info.push_back({"Encrypted", bool_text(adx.is_encrypted())});
    doc.info.push_back({"AHX routed", bool_text(adx.is_ahx())});
    doc.info.push_back({"Loop count", number(adx.loops().size())});
    for (const auto& loop : adx.loops()) {
        doc.info.push_back({
            indexed_label("Loop", loop.index),
            "type " + number(loop.type) +
                ", samples " + number(loop.start_sample) + "-" + number(loop.end_sample) +
                ", bytes " + number(loop.start_byte) + "-" + number(loop.end_byte)
        });
    }
    return doc;
}

} // namespace cristudio::modules::adx
