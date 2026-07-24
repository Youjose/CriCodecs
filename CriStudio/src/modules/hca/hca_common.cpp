#include "modules/hca/hca_common.hpp"

#include <algorithm>

namespace cristudio::modules::hca {

std::string codec_type_name(cricodecs::hca::HcaCodecChunkType type) {
    switch (type) {
    case cricodecs::hca::HcaCodecChunkType::Comp:
        return "comp";
    case cricodecs::hca::HcaCodecChunkType::Dec:
        return "dec";
    case cricodecs::hca::HcaCodecChunkType::Unknown:
        break;
    }
    return "unknown";
}

std::string quality_name(cricodecs::hca::HcaQuality quality) {
    switch (quality) {
    case cricodecs::hca::HcaQuality::Highest:
        return "highest";
    case cricodecs::hca::HcaQuality::High:
        return "high";
    case cricodecs::hca::HcaQuality::Middle:
        return "middle";
    case cricodecs::hca::HcaQuality::Low:
        return "low";
    case cricodecs::hca::HcaQuality::Lowest:
        return "lowest";
    }
    return "unknown";
}

std::optional<PcmLoop> pcm_loop(const cricodecs::hca::HcaHeader& header) {
    if (!header.loop.enabled()) {
        return std::nullopt;
    }

    const uint64_t encoded_start =
        static_cast<uint64_t>(header.loop.start_frame) * cricodecs::hca::HCA_SAMPLES_PER_FRAME +
        header.loop.start_delay;
    const uint64_t encoded_end =
        (static_cast<uint64_t>(header.loop.end_frame) + 1) * cricodecs::hca::HCA_SAMPLES_PER_FRAME -
        header.loop.end_padding;
    const uint64_t decoder_origin = header.fmt.encoder_delay;
    const uint64_t sample_count = header.sample_count();
    const auto start = (std::min)(
        encoded_start > decoder_origin ? encoded_start - decoder_origin : 0,
        sample_count
    );
    const auto end = (std::min)(
        encoded_end > decoder_origin ? encoded_end - decoder_origin : 0,
        sample_count
    );
    if (end <= start) {
        return std::nullopt;
    }
    return PcmLoop{
        .start_sample = static_cast<uint32_t>(start),
        .end_sample = static_cast<uint32_t>(end),
    };
}

} // namespace cristudio::modules::hca
