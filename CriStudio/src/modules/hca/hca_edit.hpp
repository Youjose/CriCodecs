#pragma once

#include "document/document_types.hpp"

#include "hca_codec.hpp"
#include "wav_container.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::hca {

enum class TransformAction : uint8_t {
    Decrypt,
    Encrypt,
    Rebuild
};

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decode_to_wav_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> transform_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys,
    TransformAction action
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_wav(
    const cricodecs::wav::WavContainer& wav,
    cricodecs::hca::HcaEncodeConfig config
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_pcm(
    std::span<const int16_t> pcm,
    cricodecs::hca::HcaEncodeConfig config
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    const cricodecs::hca::Hca& hca,
    const DecryptionKeys& keys,
    cricodecs::hca::HcaEncodeConfig config
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(const cricodecs::hca::Hca& hca);

} // namespace cristudio::modules::hca
