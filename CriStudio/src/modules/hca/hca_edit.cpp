#include "modules/hca/hca_edit.hpp"

namespace cristudio::modules::hca {

std::expected<std::vector<uint8_t>, std::string> decode_to_wav_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    auto hca = cricodecs::hca::Hca::load(bytes);
    if (!hca) {
        return std::unexpected(hca.error());
    }

    auto decoded = hca->decode(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    auto wav = cricodecs::wav::WavContainer::build_bytes(
        *decoded,
        hca->header().fmt.sample_rate,
        hca->header().fmt.channel_count
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
    auto hca = cricodecs::hca::Hca::load(bytes);
    if (!hca) {
        return std::unexpected(hca.error());
    }

    if (action == TransformAction::Decrypt) {
        auto plain = hca->decrypt(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
        if (!plain) {
            return std::unexpected(plain.error());
        }
        return *plain;
    }

    if (action == TransformAction::Encrypt) {
        auto encrypted = hca->encrypt(1, keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
        if (!encrypted) {
            return std::unexpected(encrypted.error());
        }
        return *encrypted;
    }

    return hca->rebuild();
}

std::expected<std::vector<uint8_t>, std::string> encode_wav(
    const cricodecs::wav::WavContainer& wav,
    cricodecs::hca::HcaEncodeConfig config
) {
    return cricodecs::hca::encode(wav, config);
}

std::expected<std::vector<uint8_t>, std::string> encode_pcm(
    std::span<const int16_t> pcm,
    cricodecs::hca::HcaEncodeConfig config
) {
    return cricodecs::hca::encode(pcm, config);
}

std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    const cricodecs::hca::Hca& hca,
    const DecryptionKeys& keys,
    cricodecs::hca::HcaEncodeConfig config
) {
    auto pcm = hca.decode(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
    if (!pcm) {
        return std::unexpected(pcm.error());
    }
    return encode_pcm(*pcm, config);
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(const cricodecs::hca::Hca& hca) {
    return hca.rebuild();
}

} // namespace cristudio::modules::hca
