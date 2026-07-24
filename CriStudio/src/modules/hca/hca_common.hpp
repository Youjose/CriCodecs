#pragma once

#include "hca_codec.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace cristudio::modules::hca {

struct PcmLoop {
    uint32_t start_sample = 0;
    uint32_t end_sample = 0;
};

[[nodiscard]] std::string codec_type_name(cricodecs::hca::HcaCodecChunkType type);
[[nodiscard]] std::string quality_name(cricodecs::hca::HcaQuality quality);
[[nodiscard]] std::optional<PcmLoop> pcm_loop(const cricodecs::hca::HcaHeader& header);

} // namespace cristudio::modules::hca
