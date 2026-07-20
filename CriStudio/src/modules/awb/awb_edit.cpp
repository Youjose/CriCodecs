#include "modules/awb/awb_edit.hpp"

#include "path_text.hpp"

#include <algorithm>

namespace cristudio::modules::awb {
namespace {

bool is_clear_m4a(std::span<const uint8_t> bytes) {
    return bytes.size() >= 12 && std::equal(bytes.begin() + 4, bytes.begin() + 8, "ftyp");
}

bool has_aac_entry(const cricodecs::awb::AwbContainer& awb, const DecryptionKeys& keys) {
    for (uint32_t index = 0; index < awb.file_count(); ++index) {
        auto data = awb.file_data(index);
        if (data && is_clear_m4a(*data)) {
            return true;
        }
        if (keys.has_cri_key) {
            auto state = awb.probe_aac_encryption(index, keys.cri_key);
            if (state && *state == cricodecs::awb::AacEncryptionState::Encrypted) {
                return true;
            }
        }
        if (awb.has_aac_key_recovery_candidate(index)) {
            return true;
        }
    }
    return false;
}

} // namespace

ScratchArchive create_scratch_archive() {
    return ScratchArchive{
        .container = cricodecs::awb::AwbContainer::create(),
        .document = LoadedDocument{
            .display_name = "NewWaveBank.awb",
            .format = "AWB/AFS2 archive (scratch)",
            .file_size = 0,
            .info = {
                {"Source", "Scratch AWB/AFS2 archive"},
                {"Files", "0"},
                {"Version", std::to_string(cricodecs::awb::AwbContainer::DEFAULT_VERSION)},
                {"Alignment", std::to_string(cricodecs::awb::AwbContainer::DEFAULT_ALIGNMENT)}
            },
            .entry_columns = {"Index", "Wave ID", "Offset", "Size"},
            .entry_column_types = {"integer", "integer", "offset", "size"},
            .entries = {}
        }
    };
}

std::vector<TransformDetailRow> detail_rows(
    const cricodecs::awb::AwbContainer& awb,
    const DecryptionKeys& keys
) {
    return {
        {QStringLiteral("Files"), QString::number(awb.file_count())},
        {QStringLiteral("Version"), QString::number(awb.version())},
        {QStringLiteral("Offset size"), QString::number(awb.offset_size())},
        {QStringLiteral("ID size"), QString::number(awb.id_size())},
        {QStringLiteral("Alignment"), QString::number(awb.alignment())},
        {QStringLiteral("Subkey"), QString::number(awb.subkey())},
        {QStringLiteral("Materialized payloads"), awb.is_materialized() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("AAC probe key"), !has_aac_entry(awb, keys)
            ? QStringLiteral("N/A")
            : keys.has_cri_key ? QStringLiteral("configured") : QStringLiteral("not configured")}
    };
}

QString aac_probe_text(
    const cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    const DecryptionKeys& keys
) {
    auto data = awb.file_data(index);
    if (!data) {
        return utf8_to_qstring(data.error());
    }
    const bool clear = is_clear_m4a(*data);
    const bool recoverable = awb.has_aac_key_recovery_candidate(index);
    if (!keys.has_cri_key) {
        return clear
            ? QStringLiteral("not encrypted")
            : recoverable ? QStringLiteral("key not configured") : QStringLiteral("N/A");
    }
    auto state = awb.probe_aac_encryption(index, keys.cri_key);
    if (!state) {
        return utf8_to_qstring(state.error());
    }
    if (*state == cricodecs::awb::AacEncryptionState::Indeterminate && !recoverable) {
        return QStringLiteral("N/A");
    }
    return utf8_to_qstring(std::string(cricodecs::awb::to_string(*state)));
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(const cricodecs::awb::AwbContainer& awb) {
    return awb.build();
}

uint32_t add_file(cricodecs::awb::AwbContainer& awb, std::span<const uint8_t> bytes) {
    return awb.add_file(bytes);
}

std::expected<void, std::string> replace_file(
    cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    return awb.replace_file(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::awb::AwbContainer& awb, uint32_t index) {
    return awb.remove_file(index);
}

std::expected<void, std::string> move_file(
    cricodecs::awb::AwbContainer& awb,
    uint32_t from_index,
    uint32_t to_index
) {
    return awb.move_file(from_index, to_index);
}

std::expected<void, std::string> set_wave_id(
    cricodecs::awb::AwbContainer& awb,
    uint32_t index,
    uint64_t wave_id
) {
    return awb.set_wave_id(index, wave_id);
}

std::expected<void, std::string> assign_wave_ids(
    cricodecs::awb::AwbContainer& awb,
    uint64_t start,
    uint64_t step
) {
    for (uint32_t index = 0; index < awb.file_count(); ++index) {
        auto result = awb.set_wave_id(index, start + step * index);
        if (!result) {
            return result;
        }
    }
    return {};
}

std::expected<void, std::string> set_build_options(
    cricodecs::awb::AwbContainer& awb,
    uint8_t version,
    uint16_t alignment,
    uint16_t subkey,
    uint8_t id_size,
    uint8_t offset_size
) {
    auto result = awb.set_version(version);
    if (result) result = awb.set_alignment(alignment);
    if (result) result = awb.set_subkey(subkey);
    if (result) result = awb.set_id_size(id_size);
    if (result) result = awb.set_offset_size(offset_size);
    return result;
}

} // namespace cristudio::modules::awb
