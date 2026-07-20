#include "modules/hca/hca_common.hpp"

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

} // namespace cristudio::modules::hca
