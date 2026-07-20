#include "modules/cvm/cvm_edit.hpp"

#include "cvm_build_script.hpp"
#include "cvm_builder.hpp"
#include "path_text.hpp"

#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cristudio::modules::cvm {
namespace {

QString hex_preview(std::span<const uint8_t> bytes, size_t max_bytes = 4096) {
    const auto count = std::min(bytes.size(), max_bytes);
    QString out;
    out.reserve(static_cast<qsizetype>(count * 3 + 64));
    for (size_t index = 0; index < count; ++index) {
        if (index != 0) {
            out += (index % 16 == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        }
        out += QStringLiteral("%1").arg(bytes[index], 2, 16, QLatin1Char('0')).toUpper();
    }
    if (bytes.size() > count) {
        out += QStringLiteral("\n... %1 more bytes").arg(static_cast<qulonglong>(bytes.size() - count));
    }
    return out;
}

QString bytes_to_hex(std::span<const uint8_t> bytes) {
    QString out;
    out.reserve(static_cast<qsizetype>(bytes.size() * 2));
    for (const auto byte : bytes) {
        out += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
    }
    return out.toUpper();
}

} // namespace

std::vector<TransformDetailRow> detail_rows(const cricodecs::cvm::CvmContainer& cvm) {
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& pv = cvm.primary_volume();
    return {
        {QStringLiteral("Disc name"), utf8_to_qstring(cvm.disc_name())},
        {QStringLiteral("Recording date"), utf8_to_qstring(cvm.recording_date_text())},
        {QStringLiteral("Media"), utf8_to_qstring(cvm.media())},
        {QStringLiteral("Scrambled"), cvm.is_scrambled() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Accessible"), cvm.has_accessible_contents() ? QStringLiteral("yes") : QStringLiteral("no")},
        {QStringLiteral("Entries"), QString::number(cvm.entry_count())},
        {QStringLiteral("ISO offset"), QString::number(cvm.embedded_iso_offset())},
        {QStringLiteral("ISO size"), QString::number(cvm.embedded_iso_size())},
        {QStringLiteral("Header flags"), QString::number(header.flags)},
        {QStringLiteral("Filesystem ID"), utf8_to_qstring(header.filesystem_id)},
        {QStringLiteral("Maker ID"), utf8_to_qstring(header.maker_id)},
        {QStringLiteral("Zone sector"), QString::number(zone.zone_sector)},
        {QStringLiteral("Data sector"), QString::number(zone.data_sector)},
        {QStringLiteral("System ID"), utf8_to_qstring(pv.system_identifier)},
        {QStringLiteral("Volume ID"), utf8_to_qstring(pv.volume_identifier)},
        {QStringLiteral("Volume set"), utf8_to_qstring(pv.volume_set_identifier)},
        {QStringLiteral("Publisher"), utf8_to_qstring(pv.publisher_identifier)},
        {QStringLiteral("Data preparer"), utf8_to_qstring(pv.data_preparer_identifier)},
        {QStringLiteral("Application"), utf8_to_qstring(pv.application_identifier)}
    };
}

std::expected<void, std::string> extract_all(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir
) {
    auto archive = cricodecs::cvm::CvmContainer::load(bytes);
    if (!archive) {
        return std::unexpected(archive.error());
    }
    return archive->extract(output_dir);
}

std::expected<std::vector<uint8_t>, std::string> save_session_bytes(
    const cricodecs::cvm::CvmContainer& cvm
) {
    return cvm.save();
}

std::expected<ImportedScript, std::string> import_build_script(
    const std::filesystem::path& script_path
) {
    auto script = cricodecs::cvm::CvmBuildScript::load(script_path);
    if (!script) {
        return std::unexpected(script.error());
    }

    cricodecs::cvm::CvmBuilder builder;
    auto built = builder.build(*script);
    if (!built) {
        return std::unexpected(built.error());
    }

    auto built_copy = *built;
    auto loaded = cricodecs::cvm::CvmContainer::load(std::move(built_copy));
    if (!loaded) {
        return std::unexpected(loaded.error());
    }

    return ImportedScript{
        .bytes = std::move(*built),
        .container = std::move(*loaded)
    };
}

std::expected<void, std::string> export_build_script(
    const cricodecs::cvm::CvmContainer& cvm,
    const std::filesystem::path& script_path
) {
    return cvm.export_script_file(script_path);
}

std::expected<void, std::string> set_metadata_options(
    cricodecs::cvm::CvmContainer& cvm,
    const MetadataOptions& options
) {
    cvm.set_disc_name(options.disc_name);
    cvm.set_recording_date(options.recording_date);
    auto media_result = cvm.set_media(options.media);
    if (!media_result) {
        return media_result;
    }
    cvm.set_system_identifier(options.system_identifier);
    cvm.set_volume_identifier(options.volume_identifier);
    cvm.set_volume_set_identifier(options.volume_set_identifier);
    cvm.set_publisher_identifier(options.publisher_identifier);
    cvm.set_data_preparer_identifier(options.data_preparer_identifier);
    cvm.set_application_identifier(options.application_identifier);
    return {};
}

std::expected<uint32_t, std::string> add_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    std::span<const uint8_t> bytes,
    const std::filesystem::path& archive_path
) {
    return cvm.add_bytes(bytes, archive_path);
}

std::expected<void, std::string> replace_bytes(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    return cvm.replace_bytes(index, bytes);
}

std::expected<void, std::string> remove_file(cricodecs::cvm::CvmContainer& cvm, uint32_t index) {
    return cvm.remove(index);
}

std::expected<void, std::string> move_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t from_index,
    uint32_t to_index
) {
    return cvm.move_file(from_index, to_index);
}

std::expected<void, std::string> rename_file(
    cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    const std::filesystem::path& archive_path
) {
    return cvm.rename(index, archive_path);
}

QString entry_preview(
    const cricodecs::cvm::CvmContainer& cvm,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    if (index >= cvm.entry_count()) {
        return hex_preview(bytes);
    }

    const auto& entry = cvm.entry(index);
    const auto& header = cvm.header();
    const auto& zone = cvm.zone();
    const auto& pv = cvm.primary_volume();
    QStringList lines;
    lines.push_back(QStringLiteral("Entry index: %1").arg(entry.index));
    lines.push_back(QStringLiteral("Archive path: %1").arg(path_to_qstring(entry.path)));
    lines.push_back(QStringLiteral("Extent sector: %1").arg(entry.extent_sector));
    lines.push_back(QStringLiteral("Declared size: %1").arg(entry.size));
    lines.push_back(QStringLiteral("Payload bytes: %1").arg(static_cast<qulonglong>(bytes.size())));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QStringLiteral("CVM session"));
    lines.push_back(QStringLiteral("Disc name: %1").arg(utf8_to_qstring(cvm.disc_name())));
    lines.push_back(QStringLiteral("Recording date: %1").arg(utf8_to_qstring(cvm.recording_date_text())));
    lines.push_back(QStringLiteral("Scrambled: %1").arg(cvm.is_scrambled() ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.push_back(QStringLiteral("Accessible contents: %1").arg(cvm.has_accessible_contents() ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.push_back(QStringLiteral("Entry count: %1").arg(cvm.entry_count()));
    lines.push_back(QStringLiteral("Embedded ISO offset: %1").arg(static_cast<qulonglong>(cvm.embedded_iso_offset())));
    lines.push_back(QStringLiteral("Embedded ISO size: %1").arg(static_cast<qulonglong>(cvm.embedded_iso_size())));
    lines.push_back(QStringLiteral("Embedded ISO sectors: %1").arg(cvm.embedded_iso_sector_count()));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QStringLiteral("CVMH header"));
    lines.push_back(QStringLiteral("Chunk length: %1").arg(static_cast<qulonglong>(header.chunk_length)));
    lines.push_back(QStringLiteral("Total size: %1").arg(static_cast<qulonglong>(header.total_size)));
    lines.push_back(QStringLiteral("Recording date bytes: %1").arg(bytes_to_hex(std::span<const uint8_t>(header.recording_date.data(), header.recording_date.size()))));
    const auto flags_hex = QString::number(header.flags, 16).toUpper();
    lines.push_back(QStringLiteral("Flags: 0x%1 (%2)").arg(flags_hex).arg(header.flags));
    lines.push_back(QStringLiteral("Filesystem ID: %1").arg(utf8_to_qstring(header.filesystem_id)));
    lines.push_back(QStringLiteral("Maker ID: %1").arg(utf8_to_qstring(header.maker_id)));
    lines.push_back(QStringLiteral("Sector table entries: %1").arg(header.sector_table_entry_count));
    lines.push_back(QStringLiteral("Zone sector index: %1").arg(header.zone_sector_index));
    lines.push_back(QStringLiteral("ISO start sector: %1").arg(header.iso_start_sector));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QStringLiteral("ZONE layout"));
    lines.push_back(QStringLiteral("Chunk length: %1").arg(static_cast<qulonglong>(zone.chunk_length)));
    lines.push_back(QStringLiteral("Zone sector: %1").arg(zone.zone_sector));
    lines.push_back(QStringLiteral("Sector length 1: %1").arg(zone.sector_length_1));
    lines.push_back(QStringLiteral("Sector length 2: %1").arg(zone.sector_length_2));
    lines.push_back(QStringLiteral("Data sector: %1").arg(zone.data_sector));
    lines.push_back(QStringLiteral("Data length: %1").arg(static_cast<qulonglong>(zone.data_length)));
    lines.push_back(QStringLiteral("ISO sector: %1").arg(zone.iso_sector));
    lines.push_back(QStringLiteral("ISO length: %1").arg(static_cast<qulonglong>(zone.iso_length)));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QStringLiteral("Primary volume"));
    lines.push_back(QStringLiteral("System identifier: %1").arg(utf8_to_qstring(pv.system_identifier)));
    lines.push_back(QStringLiteral("Volume identifier: %1").arg(utf8_to_qstring(pv.volume_identifier)));
    lines.push_back(QStringLiteral("Volume set identifier: %1").arg(utf8_to_qstring(pv.volume_set_identifier)));
    lines.push_back(QStringLiteral("Publisher identifier: %1").arg(utf8_to_qstring(pv.publisher_identifier)));
    lines.push_back(QStringLiteral("Data preparer identifier: %1").arg(utf8_to_qstring(pv.data_preparer_identifier)));
    lines.push_back(QStringLiteral("Application identifier: %1").arg(utf8_to_qstring(pv.application_identifier)));
    lines.push_back(QStringLiteral("Volume space size: %1").arg(pv.volume_space_size));
    lines.push_back(QStringLiteral("Logical block size: %1").arg(pv.logical_block_size));
    lines.push_back(QStringLiteral(""));
    lines.push_back(QStringLiteral("Hex preview"));
    lines.push_back(hex_preview(bytes));
    return lines.join(QLatin1Char('\n'));
}

} // namespace cristudio::modules::cvm
