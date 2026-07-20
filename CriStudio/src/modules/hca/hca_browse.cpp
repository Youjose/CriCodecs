#include "modules/hca/hca_browse.hpp"

#include "modules/hca/hca_common.hpp"
#include "shared/document_helpers.hpp"

#include <iomanip>
#include <sstream>

namespace cristudio::modules::hca {

namespace {

std::string hex_u64(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::hca::Hca& hca) {
    auto doc = base_document(path, "HCA audio");
    const auto& header = hca.header();
    doc.info.push_back({"Version", hex_u64(header.file.version)});
    doc.info.push_back({"Header size", number(header.file.header_size)});
    doc.info.push_back({"Channels", number(header.fmt.channel_count)});
    doc.info.push_back({"Sample rate", number(header.fmt.sample_rate)});
    doc.info.push_back({"Frames", number(header.fmt.frame_count)});
    doc.info.push_back({"Samples", number(header.sample_count())});
    doc.info.push_back({"Encoder delay", number(header.fmt.encoder_delay)});
    doc.info.push_back({"Encoder padding", number(header.fmt.encoder_padding)});
    doc.info.push_back({"Codec type", codec_type_name(header.codec.type())});
    doc.info.push_back({"Frame size", number(header.codec.frame_size)});
    doc.info.push_back({"Resolution", number(header.codec.min_resolution) + "-" + number(header.codec.max_resolution)});
    doc.info.push_back({"Track count", number(header.codec.track_count)});
    doc.info.push_back({"Channel config", number(header.codec.channel_config)});
    doc.info.push_back({"Bands", "total " + number(header.codec.total_band_count) +
        ", base " + number(header.codec.base_band_count) +
        ", stereo " + number(header.codec.stereo_band_count) +
        ", hfr " + number(header.codec.bands_per_hfr_group) +
        " x " + number(header.codec.hfr_group_count)});
    doc.info.push_back({"MS stereo", bool_text(header.codec.uses_ms_stereo())});
    doc.info.push_back({"VBR", header.vbr.enabled()
        ? "max frame " + number(header.vbr.max_frame_size) + ", noise " + number(header.vbr.noise_level)
        : "no"});
    doc.info.push_back({"ATH", "type " + number(header.ath.type) + ", curve " + bool_text(header.ath.uses_curve())});
    doc.info.push_back({"Cipher type", number(header.cipher.type)});
    doc.info.push_back({"Loop", header.loop.enabled()
        ? "frames " + number(header.loop.start_frame) + "-" + number(header.loop.end_frame) +
            ", delay " + number(header.loop.start_delay) +
            ", padding " + number(header.loop.end_padding)
        : "no"});
    doc.info.push_back({"RVA volume", float_text(header.rva.volume)});
    doc.info.push_back({"Comment length", number(header.comment.length)});
    return doc;
}

} // namespace cristudio::modules::hca
