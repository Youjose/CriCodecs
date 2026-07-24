#include "modules/hca/hca_preview.hpp"

#include "modules/hca/hca_common.hpp"
#include "shared/audio_preview_helpers.hpp"
#include "wav_container.hpp"

#include <utility>

namespace cristudio::modules::hca {
namespace {

std::vector<cricodecs::wav::SampleLoop> wav_loops_from_hca(
    const cricodecs::hca::HcaHeader& header
) {
    const auto loop = pcm_loop(header);
    if (!loop.has_value()) {
        return {};
    }

    return {{
        .cue_point_id = 0,
        .type = 0,
        .start = loop->start_sample,
        .end = loop->end_sample,
        .fraction = 0,
        .play_count = 0,
    }};
}

} // namespace

std::expected<AudioPreview, std::string> audio_preview(
    const cricodecs::hca::Hca& hca,
    const DecryptionKeys& keys
) {
    const auto& header = hca.header();
    if (header.cipher.type != 0 && header.cipher.type != 1 && !keys.has_cri_key) {
        return std::unexpected("HCA preview needs a decryption key");
    }

    auto pcm = hca.decode(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
    if (!pcm) {
        return std::unexpected("HCA decode failed: " + pcm.error());
    }

    const auto loops = wav_loops_from_hca(header);
    auto wav_bytes = cricodecs::wav::WavContainer::build_bytes(
        *pcm,
        header.fmt.sample_rate,
        header.fmt.channel_count,
        loops
    );
    if (!wav_bytes) {
        return std::unexpected("HCA WAV preview build failed: " + wav_bytes.error());
    }

    const auto channels = static_cast<uint16_t>(header.fmt.channel_count);
    const auto sample_count = channels == 0 ? 0 : static_cast<uint64_t>(pcm->size() / channels);
    return make_wav_audio_preview(
        std::move(*wav_bytes),
        header.fmt.sample_rate,
        channels,
        sample_count,
        "HCA audio",
        {},
        audio_loops_from_wav_loops(loops, sample_count)
    );
}

std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    auto hca = cricodecs::hca::Hca::load(bytes);
    if (!hca) {
        return std::unexpected("HCA preview failed: " + hca.error());
    }
    return audio_preview(*hca, keys);
}

std::expected<AudioPreview, std::string> audio_preview_from_file(
    const std::filesystem::path& path,
    const DecryptionKeys& keys
) {
    auto hca = cricodecs::hca::Hca::load(path);
    if (!hca) {
        return std::unexpected("HCA preview failed: " + hca.error());
    }
    return audio_preview(*hca, keys);
}

} // namespace cristudio::modules::hca
