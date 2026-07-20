#pragma once

#include "hca_codec.hpp"

#include <string>

namespace cristudio::modules::hca {

[[nodiscard]] std::string codec_type_name(cricodecs::hca::HcaCodecChunkType type);
[[nodiscard]] std::string quality_name(cricodecs::hca::HcaQuality quality);

} // namespace cristudio::modules::hca
