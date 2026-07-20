#include "modules/adx/adx_common.hpp"

#include "path_text.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cristudio::modules::adx {
namespace {

QString hex_preview(std::span<const uint8_t> bytes, size_t max_bytes = 4096) {
    if (bytes.empty()) {
        return QStringLiteral("(no bytes)");
    }

    const auto shown = std::min(bytes.size(), max_bytes);
    QString out;
    out.reserve(static_cast<qsizetype>(shown * 4));
    for (size_t offset = 0; offset < shown; offset += 16) {
        out += QStringLiteral("%1  ").arg(static_cast<qulonglong>(offset), 8, 16, QLatin1Char('0')).toUpper();
        const auto row_end = std::min(offset + 16, shown);
        for (size_t index = offset; index < row_end; ++index) {
            out += QStringLiteral("%1 ").arg(bytes[index], 2, 16, QLatin1Char('0')).toUpper();
        }
        out += QLatin1Char('\n');
    }
    if (bytes.size() > shown) {
        out += QStringLiteral("... truncated, %1 total bytes ...\n").arg(static_cast<qulonglong>(bytes.size()));
    }
    return out;
}

} // namespace

void apply_keys(cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    switch (keys.adx_mode) {
    case DecryptionKeys::AdxMode::Type8String:
        adx.set_key_type8(keys.adx_type8_key);
        break;
    case DecryptionKeys::AdxMode::Type9Number:
        adx.set_key_type9(keys.adx_type9_key, keys.adx_subkey);
        break;
    case DecryptionKeys::AdxMode::AhxTriplet:
        if (adx.is_ahx()) {
            adx.set_ahx_key(keys.ahx_start, keys.ahx_mult, keys.ahx_add);
        } else {
            adx.set_key_triplet(keys.ahx_start, keys.ahx_mult, keys.ahx_add);
        }
        break;
    case DecryptionKeys::AdxMode::None:
        break;
    }
}

bool has_compatible_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    if (!adx.is_encrypted()) {
        return true;
    }
    if (keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet) {
        return true;
    }
    if (adx.header().flags == 8) {
        return keys.adx_mode == DecryptionKeys::AdxMode::Type8String;
    }
    if (adx.header().flags == 9) {
        return keys.adx_mode == DecryptionKeys::AdxMode::Type9Number;
    }
    return false;
}

bool has_applicable_raw_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    return adx.is_encrypted() && has_compatible_key(adx, keys);
}

QString payload_preview(QString label, std::span<const uint8_t> bytes) {
    QString out;
    out += QStringLiteral("%1\n").arg(std::move(label));
    out += QStringLiteral("Payload bytes: %1\n").arg(static_cast<qulonglong>(bytes.size()));

    auto adx = cricodecs::adx::Adx::load(bytes);
    if (!adx) {
        out += QStringLiteral("ADX parse: %1\n\n").arg(utf8_to_qstring(adx.error()));
        out += hex_preview(bytes);
        return out;
    }

    const auto& header = adx->header();
    const auto signature_text = QStringLiteral("%1").arg(header.signature, 4, 16, QLatin1Char('0')).toUpper();
    const auto flags_text = QStringLiteral("%1").arg(header.flags, 2, 16, QLatin1Char('0')).toUpper();
    out += QStringLiteral("Signature: 0x%1\n").arg(signature_text);
    out += QStringLiteral("Data offset: %1\n").arg(header.data_offset);
    out += QStringLiteral("Encoding mode: %1%2\n")
        .arg(header.encoding_mode)
        .arg(adx->is_ahx() ? QStringLiteral(" (AHX)") : QString{});
    out += QStringLiteral("Block size / bit depth: %1 / %2\n").arg(header.block_size).arg(header.bit_depth);
    out += QStringLiteral("Channels: %1\n").arg(header.channels);
    out += QStringLiteral("Sample rate: %1\n").arg(header.sample_rate);
    out += QStringLiteral("Sample count: %1\n").arg(header.sample_count);
    out += QStringLiteral("Highpass frequency: %1\n").arg(header.highpass_freq);
    out += QStringLiteral("Version: %1\n").arg(header.version);
    out += QStringLiteral("Flags: 0x%1\n").arg(flags_text);
    out += QStringLiteral("Encrypted: %1\n").arg(adx->is_encrypted() ? QStringLiteral("yes") : QStringLiteral("no"));
    out += QStringLiteral("Loop count: %1\n").arg(static_cast<qsizetype>(adx->loops().size()));
    for (const auto& loop : adx->loops()) {
        out += QStringLiteral("  Loop %1: type %2, samples %3-%4, bytes %5-%6\n")
            .arg(loop.index)
            .arg(loop.type)
            .arg(loop.start_sample)
            .arg(loop.end_sample)
            .arg(loop.start_byte)
            .arg(loop.end_byte);
    }
    out += QStringLiteral("\nHex preview\n");
    out += hex_preview(bytes);
    return out;
}

} // namespace cristudio::modules::adx
