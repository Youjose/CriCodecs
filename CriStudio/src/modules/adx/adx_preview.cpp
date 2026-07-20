#include "modules/adx/adx_preview.hpp"

#include "modules/adx/adx_common.hpp"
#include "shared/audio_preview_helpers.hpp"
#include "wav_container.hpp"

#include <utility>

namespace cristudio::modules::adx {

std::vector<cricodecs::wav::SampleLoop> wav_loops_from_adx(const cricodecs::adx::AdxDecodeResult& decoded) {
    std::vector<cricodecs::wav::SampleLoop> loops;
    if (!decoded.has_loops) {
        return loops;
    }
    loops.reserve(decoded.loops.size());
    for (const auto& loop : decoded.loops) {
        loops.push_back(loop.to_sample_loop());
    }
    return loops;
}

std::expected<AudioPreview, std::string> audio_preview(cricodecs::adx::Adx adx, const DecryptionKeys& keys) {
    const bool needs_key = adx.is_encrypted() && !has_applicable_raw_key(adx, keys);
    apply_keys(adx, keys);
    if (needs_key) {
        return std::unexpected("ADX/AHX preview needs a decryption key");
    }

    auto decoded = adx.decode();
    if (!decoded) {
        return std::unexpected("ADX/AHX decode failed: " + decoded.error());
    }

    auto loops = wav_loops_from_adx(*decoded);
    auto wav_bytes = cricodecs::wav::WavContainer::build_bytes(
        decoded->pcm_data,
        decoded->sample_rate,
        decoded->channels,
        loops
    );
    if (!wav_bytes) {
        return std::unexpected("ADX/AHX WAV preview build failed: " + wav_bytes.error());
    }

    return make_wav_audio_preview(
        std::move(*wav_bytes),
        decoded->sample_rate,
        decoded->channels,
        decoded->sample_count,
        adx.is_ahx() ? "AHX audio" : "ADX audio",
        {},
        audio_loops_from_wav_loops(loops, decoded->sample_count)
    );
}

std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    auto adx = cricodecs::adx::Adx::load(bytes);
    if (!adx) {
        return std::unexpected("ADX/AHX preview failed: " + adx.error());
    }
    return audio_preview(std::move(*adx), keys);
}

std::expected<AudioPreview, std::string> audio_preview_from_file(
    const std::filesystem::path& path,
    const DecryptionKeys& keys
) {
    auto adx = cricodecs::adx::Adx::load(path);
    if (!adx) {
        return std::unexpected("ADX/AHX preview failed: " + adx.error());
    }
    return audio_preview(std::move(*adx), keys);
}

} // namespace cristudio::modules::adx
