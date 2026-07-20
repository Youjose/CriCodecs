#pragma once

#include "document/document_types.hpp"

#include "adx_codec.hpp"
#include "wav_container.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::adx {

enum class TransformAction : uint8_t {
    Decrypt,
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
    const cricodecs::adx::AdxEncodeConfig& config
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(
    cricodecs::adx::Adx& adx,
    const DecryptionKeys& keys,
    cricodecs::adx::AdxEncodeConfig config
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(cricodecs::adx::Adx& adx);

void apply_config_keys(cricodecs::adx::AdxEncodeConfig& config, const DecryptionKeys& keys);

} // namespace cristudio::modules::adx
