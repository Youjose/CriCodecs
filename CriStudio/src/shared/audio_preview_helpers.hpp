#pragma once

#include "document/document_types.hpp"

#include "wav_container.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cristudio {

[[nodiscard]] AudioPreview make_wav_audio_preview(
    std::vector<uint8_t> wav_bytes,
    uint32_t sample_rate,
    uint16_t channels,
    uint64_t sample_count,
    std::string format,
    std::string note = {},
    std::vector<AudioLoop> loops = {}
);

[[nodiscard]] std::vector<AudioLoop> audio_loops_from_wav_loops(
    std::span<const cricodecs::wav::SampleLoop> loops,
    uint64_t sample_count
);

} // namespace cristudio
