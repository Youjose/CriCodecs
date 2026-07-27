#include <QCoreApplication>
#include "modules/audio/audio_encode.hpp"

#include "modules/adx/adx_edit.hpp"
#include "modules/hca/hca_common.hpp"
#include "modules/hca/hca_edit.hpp"
#include "path_text.hpp"
#include "shared/document_extract_helpers.hpp"

#include "adx_codec.hpp"
#include "wav_container.hpp"

#include <system_error>
#include <utility>
#include <vector>

namespace cristudio::modules::audio {
namespace {

void push_log(const EncodeLogCallback& log, QString message) {
    if (log) {
        log(std::move(message));
    }
}

QString yes_no(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

} // namespace

std::expected<void, QString> encode_from_wav(EncodeConfig config, EncodeLogCallback log) {
    if (config.input_wav.empty()) {
        return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "Choose a WAV input path."));
    }
    if (config.output_path.empty()) {
        return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "Choose an output path."));
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.input_wav, ec) || ec) {
        return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "WAV input does not exist: %1").arg(path_to_qstring(config.input_wav)));
    }

    cricodecs::wav::WavContainer wav;
    push_log(log, QCoreApplication::translate("Audio.AudioEncode", "Loading WAV source: %1").arg(path_to_qstring(config.input_wav)));
    if (auto result = wav.load(config.input_wav); !result) {
        return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "WAV load failed: %1").arg(utf8_to_qstring(result.error())));
    }
    push_log(log, QCoreApplication::translate(
        "Audio.AudioEncode",
        "WAV source: sample rate %1 Hz, channels %2, sample frames %3.")
        .arg(wav.sample_rate())
        .arg(wav.channels())
        .arg(static_cast<qulonglong>(wav.sample_count())));

    std::expected<std::vector<uint8_t>, std::string> encoded;
    if (config.target == EncodeTarget::Adx) {
        cricodecs::adx::AdxEncodeConfig adx_config;
        adx_config.encoding_mode = config.adx_encoding_mode;
        adx_config.version = config.adx_version;
        adx_config.highpass_freq = config.adx_highpass_frequency;
        adx_config.delete_samples_after_loop_end = config.adx_delete_after_loop;
        if (config.adx_encrypt_type8) {
            if (config.keys.adx_type8_key.empty()) {
                return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "ADX type-8 encoding needs a configured type-8 key string."));
            }
            adx_config.encryption_type = 8;
        }
        modules::adx::apply_config_keys(adx_config, config.keys);

        const auto mode_text = QStringLiteral("%1").arg(config.adx_encoding_mode, 2, 16, QLatin1Char('0')).toUpper();
        push_log(log, QCoreApplication::translate("Audio.AudioEncode", "Encoding %1 from WAV: mode 0x%2, version %3, highpass %4 Hz%5.")
            .arg(config.adx_encoding_mode == 0x10 || config.adx_encoding_mode == 0x11 ? QStringLiteral("AHX") : QStringLiteral("ADX"))
            .arg(mode_text)
            .arg(config.adx_version)
            .arg(config.adx_highpass_frequency)
            .arg(config.adx_encrypt_type8 ? QCoreApplication::translate("Audio.AudioEncode", ", encrypted type 8") : QString{}));
        encoded = modules::adx::encode_wav(wav, adx_config);
    } else if (config.target == EncodeTarget::Hca) {
        auto hca_config = config.hca_config;
        if (config.hca_encrypt) {
            if (!config.keys.has_cri_key) {
                return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "HCA encryption needs a configured CRI key."));
            }
            hca_config.keycode = config.keys.cri_key;
            hca_config.subkey = config.keys.hca_subkey;
        }

        const auto version_text = QStringLiteral("%1").arg(hca_config.version, 4, 16, QLatin1Char('0')).toUpper();
        push_log(log, QCoreApplication::translate("Audio.AudioEncode", "Encoding HCA from WAV: version 0x%1, quality %2, bitrate %3, M/S %4, loop %5%6.")
            .arg(version_text)
            .arg(QString::fromStdString(modules::hca::quality_name(hca_config.quality)))
            .arg(hca_config.bitrate == 0 ? QCoreApplication::translate("Audio.AudioEncode", "auto") : QString::number(hca_config.bitrate))
            .arg(yes_no(hca_config.ms_stereo))
            .arg(yes_no(hca_config.loop_enabled))
            .arg(config.hca_encrypt ? QCoreApplication::translate("Audio.AudioEncode", ", encrypted") : QString{}));
        encoded = modules::hca::encode_wav(wav, hca_config);
    } else {
        return std::unexpected(QCoreApplication::translate("Audio.AudioEncode", "WAV encode only supports ADX/AHX and HCA targets."));
    }

    if (!encoded) {
        return std::unexpected(utf8_to_qstring(encoded.error()));
    }
    if (auto result = write_binary_file(config.output_path, *encoded); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    push_log(log, QCoreApplication::translate("Audio.AudioEncode", "Encoded %1 bytes to %2.")
        .arg(static_cast<qulonglong>(encoded->size()))
        .arg(path_to_qstring(config.output_path)));
    return {};
}

std::vector<TransformDetailRow> encode_job_detail_rows(const DecryptionKeys& keys) {
    return {
        {QStringLiteral("Job"), QCoreApplication::translate("Audio.AudioEncode", "Encode WAV input to ADX, AHX, or HCA output")},
        {QStringLiteral("Input"), QCoreApplication::translate("Audio.AudioEncode", "PCM WAV")},
        {QStringLiteral("Outputs"), QCoreApplication::translate("Audio.AudioEncode", ".adx, .ahx, .hca")},
        {
            QStringLiteral("Keys"),
            keys.has_cri_key || !keys.adx_type8_key.empty()
                ? QCoreApplication::translate("Audio.AudioEncode", "configured keys available")
                : QCoreApplication::translate("Audio.AudioEncode", "no encryption keys configured")
        }
    };
}

} // namespace cristudio::modules::audio
