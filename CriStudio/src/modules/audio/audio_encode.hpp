#pragma once

#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include "hca_codec.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <vector>

namespace cristudio::modules::audio {

enum class EncodeTarget : uint8_t {
    Adx,
    Hca
};

struct EncodeConfig {
    EncodeTarget target = EncodeTarget::Adx;
    std::filesystem::path input_wav;
    std::filesystem::path output_path;
    DecryptionKeys keys;

    uint8_t adx_encoding_mode = 3;
    uint8_t adx_version = 4;
    uint16_t adx_highpass_frequency = 500;
    bool adx_encrypt_type8 = false;
    bool adx_delete_after_loop = false;

    cricodecs::hca::HcaEncodeConfig hca_config;
    bool hca_encrypt = false;
};

using EncodeLogCallback = std::function<void(QString)>;

[[nodiscard]] std::expected<void, QString> encode_from_wav(
    EncodeConfig config,
    EncodeLogCallback log
);

[[nodiscard]] std::vector<TransformDetailRow> encode_job_detail_rows(const DecryptionKeys& keys);

} // namespace cristudio::modules::audio
