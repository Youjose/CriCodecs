#include "modules/acb/acb_edit.hpp"

#include "awb_container.hpp"
#include "io_reader.hpp"
#include "modules/utf/utf_edit_ui.hpp"
#include "path_text.hpp"

#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cristudio::modules::acb {
namespace {

QString waveform_summary(const cricodecs::acb::AcbContainer& acb, uint32_t index, const cricodecs::acb::WaveformInfo& waveform) {
    QStringList parts;
    parts.reserve(8);

    const auto name = utf8_to_qstring(std::string(acb.waveform_name(index)));
    const auto raw_name = utf8_to_qstring(std::string(acb.waveform_name_raw(index)));
    const auto file_name = utf8_to_qstring(acb.waveform_filename(index));

    if (!name.isEmpty()) {
        parts.push_back(QStringLiteral("name %1").arg(name));
    }
    if (!raw_name.isEmpty() && raw_name != name) {
        parts.push_back(QStringLiteral("raw %1").arg(raw_name));
    }
    if (!file_name.isEmpty() && file_name != name) {
        parts.push_back(QStringLiteral("file %1").arg(file_name));
    }

    parts.push_back(QStringLiteral("id %1").arg(waveform.id));
    parts.push_back(QStringLiteral("mem %1").arg(waveform.memory_awb_id));
    parts.push_back(QStringLiteral("stream %1").arg(waveform.stream_awb_id));
    parts.push_back(QStringLiteral("encode %1%2")
        .arg(waveform.encode_type)
        .arg(utf8_to_qstring(std::string(cricodecs::acb::encode_type_extension(waveform.encode_type)))));
    if (waveform.loop_flag) {
        parts.push_back(QStringLiteral("loop"));
    }
    return parts.join(QStringLiteral(", "));
}

QString cue_map_summary(const cricodecs::acb::WaveNameInfo& name) {
    QStringList parts;
    parts.reserve(5);
    parts.push_back(QStringLiteral("waveform %1").arg(name.waveform_index));
    parts.push_back(QStringLiteral("wave id %1").arg(name.wave_id));
    parts.push_back(QStringLiteral("port %1").arg(name.port_no));
    parts.push_back(QStringLiteral("encode %1").arg(name.encode_type));
    const auto display_name = utf8_to_qstring(name.name.empty() ? name.name_raw : name.name);
    if (!display_name.isEmpty()) {
        parts.push_back(display_name);
    }
    return parts.join(QStringLiteral(", "));
}

QString safe_awb_output_name(QString name) {
    name = name.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("associated");
    }
    for (auto& ch : name) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':') ||
            ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')) {
            ch = QLatin1Char('_');
        }
    }
    if (!name.endsWith(QStringLiteral(".awb"), Qt::CaseInsensitive)) {
        name += QStringLiteral(".awb");
    }
    return name;
}

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

} // namespace

QString associated_awb_default_name(const cricodecs::acb::AcbContainer& acb, QString title) {
    if (auto path = acb.companion_awb_path()) {
        return path_to_qstring(path->filename());
    }
    title = title.trimmed();
    if (title.endsWith(QStringLiteral(".acb"), Qt::CaseInsensitive)) {
        title.chop(4);
    }
    return safe_awb_output_name(title + QStringLiteral("_associated"));
}

std::expected<AssociatedAwbBytes, QString> associated_awb_bytes(const cricodecs::acb::AcbContainer& acb) {
    if (auto embedded = acb.embedded_awb()) {
        return AssociatedAwbBytes{
            .bytes = std::vector<uint8_t>(embedded->begin(), embedded->end()),
            .source_path = std::nullopt
        };
    }
    if (auto path = acb.companion_awb_path()) {
        auto bytes = cricodecs::io::read_file_bytes(*path, "ACB associated AWB failed");
        if (!bytes) {
            return std::unexpected(utf8_to_qstring(bytes.error()));
        }
        return AssociatedAwbBytes{
            .bytes = std::move(*bytes),
            .source_path = *path
        };
    }
    auto loaded = acb.load_awb();
    if (!loaded) {
        return std::unexpected(utf8_to_qstring(loaded.error()));
    }
    return std::unexpected(QStringLiteral("Associated AWB resolved but no source bytes were available."));
}

std::expected<void, QString> validate_associated_awb(std::span<const uint8_t> bytes) {
    auto awb = cricodecs::awb::AwbContainer::load(bytes);
    if (!awb) {
        return std::unexpected(utf8_to_qstring(awb.error()));
    }
    return {};
}

std::vector<TransformDetailRow> detail_rows(
    const cricodecs::acb::AcbContainer& acb,
    const DecryptionKeys& keys
) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QStringLiteral("Header table"), utf8_to_qstring(std::string(acb.name()))});
    rows.push_back({QStringLiteral("Waveforms"), QString::number(acb.waveform_count())});
    rows.push_back({QStringLiteral("Resolved names"), QString::number(static_cast<qsizetype>(acb.wave_names().size()))});
    rows.push_back({QStringLiteral("Embedded AWB"), acb.has_embedded_awb() ? QStringLiteral("yes") : QStringLiteral("no")});
    if (auto embedded = acb.embedded_awb()) {
        rows.push_back({QStringLiteral("Embedded AWB bytes"), QString::number(static_cast<qulonglong>(embedded->size()))});
    }
    if (auto path = acb.companion_awb_path()) {
        rows.push_back({QStringLiteral("Companion AWB"), path_to_qstring(*path)});
    }
    if (auto awb = acb.load_awb()) {
        rows.push_back({
            QStringLiteral("Associated AWB"),
            QStringLiteral("%1 file(s), version %2, alignment %3, id size %4, offset size %5")
                .arg(awb->file_count())
                .arg(awb->version())
                .arg(awb->alignment())
                .arg(awb->id_size())
                .arg(awb->offset_size())
        });
    } else {
        rows.push_back({QStringLiteral("Associated AWB"), utf8_to_qstring(awb.error())});
    }

    for (uint32_t index = 0; index < acb.waveform_count(); ++index) {
        const auto& waveform = acb.waveform(index);
        QString aac_state = QStringLiteral("N/A");
        if (waveform.encode_type == 19) {
            if (auto state = acb.probe_waveform_aac_encryption(
                    index, keys.has_cri_key ? keys.cri_key : 0)) {
                if (*state == cricodecs::awb::AacEncryptionState::Clear || keys.has_cri_key) {
                    aac_state = utf8_to_qstring(std::string(cricodecs::awb::to_string(*state)));
                } else {
                    aac_state = QStringLiteral("key not configured");
                }
            } else {
                aac_state = utf8_to_qstring(state.error());
            }
        }
        rows.push_back({
            QStringLiteral("Waveform %1").arg(index),
            waveform_summary(acb, index, waveform) +
                QStringLiteral(", port %1, streaming %2, ext %3, AAC %4")
                    .arg(waveform.port_no)
                    .arg(waveform.streaming)
                    .arg(waveform.extension_data)
                    .arg(aac_state),
            6,
            static_cast<int>(index)
        });
    }

    for (const auto& name : acb.wave_names()) {
        rows.push_back({
            QStringLiteral("Cue map"),
            cue_map_summary(name) + QStringLiteral(", streaming %1").arg(name.streaming),
            6,
            static_cast<int>(name.waveform_index)
        });
    }
    return rows;
}

std::expected<cricodecs::utf::UtfTable, QString> payload_table(
    const cricodecs::acb::AcbContainer& acb,
    int payload_kind,
    int index
) {
    if (payload_kind != 7) {
        return std::unexpected(QStringLiteral("ACB table preview failed: unsupported payload kind"));
    }
    if (index < 0) {
        return acb.header_table();
    }

    const auto subtables = acb.subtable_info();
    if (index >= static_cast<int>(subtables.size())) {
        return std::unexpected(QStringLiteral("ACB subtable preview failed: index out of range"));
    }

    const auto& info = subtables[static_cast<size_t>(index)];
    auto table = acb.subtable(info.name);
    if (!table) {
        return std::unexpected(QStringLiteral("ACB subtable preview failed: %1 is not present")
            .arg(utf8_to_qstring(info.name)));
    }
    return table->get();
}

std::expected<QString, QString> payload_preview(
    const cricodecs::acb::AcbContainer& acb,
    const DecryptionKeys& keys,
    int payload_kind,
    int index
) {
    if (payload_kind == 6) {
        if (index < 0) {
            return std::unexpected(QStringLiteral("ACB waveform preview failed: index out of range"));
        }
        auto data = acb.extract_waveform_data(static_cast<uint32_t>(index), keys.has_cri_key ? keys.cri_key : 0);
        if (!data) {
            return std::unexpected(QStringLiteral("ACB waveform preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return hex_preview(std::span<const uint8_t>(data->data(), data->size()));
    }

    if (payload_kind == 7) {
        if (index < 0) {
            return ::cristudio::modules::utf::utf_table_preview(acb.header_table());
        }

        const auto subtables = acb.subtable_info();
        if (index >= static_cast<int>(subtables.size())) {
            return std::unexpected(QStringLiteral("ACB subtable preview failed: index out of range"));
        }

        const auto& info = subtables[static_cast<size_t>(index)];
        auto table = acb.subtable(info.name);
        if (!table) {
            return std::unexpected(QStringLiteral("ACB subtable preview failed: %1 is not present")
                .arg(utf8_to_qstring(info.name)));
        }
        return ::cristudio::modules::utf::utf_table_preview(table->get());
    }

    return std::unexpected(QStringLiteral("ACB preview failed: unsupported payload kind"));
}

} // namespace cristudio::modules::acb
