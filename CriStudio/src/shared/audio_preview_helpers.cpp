#include "shared/audio_preview_helpers.hpp"

#include "shared/document_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cristudio {

AudioPreview make_wav_audio_preview(
    std::vector<uint8_t> wav_bytes,
    uint32_t sample_rate,
    uint16_t channels,
    uint64_t sample_count,
    std::string format,
    std::string note,
    std::vector<AudioLoop> loops
) {
    AudioPreview preview;
    preview.wav_bytes = std::move(wav_bytes);
    preview.sample_rate = sample_rate;
    preview.channels = channels;
    preview.sample_count = sample_count;
    preview.format = std::move(format);
    preview.note = std::move(note);
    preview.loops = std::move(loops);
    return preview;
}

std::vector<AudioLoop> audio_loops_from_wav_loops(
    std::span<const cricodecs::wav::SampleLoop> loops,
    uint64_t sample_count
) {
    std::vector<AudioLoop> result;
    result.reserve(loops.size());
    for (size_t i = 0; i < loops.size(); ++i) {
        const auto& loop = loops[i];
        const auto start = std::min<uint64_t>(loop.start, sample_count);
        const auto end = std::min<uint64_t>(loop.end, sample_count);
        if (end <= start) {
            continue;
        }
        result.push_back({
            "Loop " + number(i + 1) + " [" + number(start) + " - " + number(end) + "]",
            start,
            end
        });
    }
    return result;
}

} // namespace cristudio
