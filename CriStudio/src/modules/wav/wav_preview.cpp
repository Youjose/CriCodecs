#include "modules/wav/wav_preview.hpp"

#include "shared/audio_preview_helpers.hpp"
#include "shared/document_helpers.hpp"
#include "wav_container.hpp"

#include <vector>

namespace cristudio::modules::wav {

std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& source_name
) {
    cricodecs::wav::WavContainer wav;
    if (auto loaded = wav.load(bytes); !loaded) {
        return std::unexpected("WAV preview failed: " + loaded.error());
    }
    return make_wav_audio_preview(
        std::vector<uint8_t>(bytes.begin(), bytes.end()),
        wav.sample_rate(),
        static_cast<uint16_t>(wav.channels()),
        wav.sample_count(),
        "WAV audio",
        generic_path(source_name),
        audio_loops_from_wav_loops(wav.sampler().loops, wav.sample_count())
    );
}

std::expected<AudioPreview, std::string> audio_preview_from_file(const std::filesystem::path& path) {
    cricodecs::wav::WavContainer wav;
    if (auto loaded = wav.load(path); !loaded) {
        return std::unexpected("WAV preview failed: " + loaded.error());
    }

    AudioPreview preview;
    preview.playable_path = path;
    preview.sample_rate = wav.sample_rate();
    preview.channels = static_cast<uint16_t>(wav.channels());
    preview.sample_count = wav.sample_count();
    preview.format = "WAV audio";
    preview.note = generic_path(path);
    preview.loops = audio_loops_from_wav_loops(wav.sampler().loops, wav.sample_count());
    return preview;
}

} // namespace cristudio::modules::wav
