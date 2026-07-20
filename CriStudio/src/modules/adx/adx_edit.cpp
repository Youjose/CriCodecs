#include "modules/adx/adx_edit.hpp"

#include "modules/adx/adx_common.hpp"
#include "modules/adx/adx_preview.hpp"

namespace cristudio::modules::adx {

std::expected<std::vector<uint8_t>, std::string> decode_to_wav_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    auto adx = cricodecs::adx::Adx::load(bytes);
    if (!adx) {
        return std::unexpected(adx.error());
    }
    apply_keys(*adx, keys);

    auto decoded = adx->decode();
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    auto loops = wav_loops_from_adx(*decoded);
    auto wav = cricodecs::wav::WavContainer::build_bytes(
        decoded->pcm_data,
        decoded->sample_rate,
        decoded->channels,
        loops
    );
    if (!wav) {
        return std::unexpected(wav.error());
    }
    return *wav;
}

std::expected<std::vector<uint8_t>, std::string> transform_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys,
    TransformAction action
) {
    auto adx = cricodecs::adx::Adx::load(bytes);
    if (!adx) {
        return std::unexpected(adx.error());
    }
    apply_keys(*adx, keys);

    if (action == TransformAction::Decrypt) {
        auto plain = adx->decrypt();
        if (!plain) {
            return std::unexpected(plain.error());
        }
        return *plain;
    }

    return adx->rebuild();
}

std::expected<std::vector<uint8_t>, std::string> encode_wav(
    const cricodecs::wav::WavContainer& wav,
    const cricodecs::adx::AdxEncodeConfig& config
) {
    return cricodecs::adx::AdxEncoder::encode(wav, config);
}

void apply_config_keys(cricodecs::adx::AdxEncodeConfig& config, const DecryptionKeys& keys) {
    config.key_string = keys.adx_type8_key;
    config.key64 = keys.adx_type9_key;
    config.subkey = keys.adx_subkey;
    config.ahx_key = cricodecs::ahx::AhxKey{
        .start = keys.ahx_start,
        .mult = keys.ahx_mult,
        .add = keys.ahx_add
    };
}

std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    cricodecs::adx::Adx& adx,
    const DecryptionKeys& keys,
    cricodecs::adx::AdxEncodeConfig config
) {
    apply_keys(adx, keys);
    apply_config_keys(config, keys);
    return adx.encode(config);
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(cricodecs::adx::Adx& adx) {
    return adx.rebuild();
}

} // namespace cristudio::modules::adx
