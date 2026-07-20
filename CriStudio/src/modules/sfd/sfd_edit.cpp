#include "modules/sfd/sfd_edit.hpp"

#include "path_text.hpp"

#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <span>

namespace cristudio::modules::sfd {
namespace {

QString stream_type_name(cricodecs::sfd::SfdStreamType type) {
    switch (type) {
    case cricodecs::sfd::SfdStreamType::audio: return QStringLiteral("audio");
    case cricodecs::sfd::SfdStreamType::video: return QStringLiteral("video");
    case cricodecs::sfd::SfdStreamType::private_data: return QStringLiteral("private");
    }
    return QStringLiteral("unknown");
}

QString audio_type_name(cricodecs::sfd::SfdAudioType type) {
    switch (type) {
    case cricodecs::sfd::SfdAudioType::adx: return QStringLiteral("adx");
    case cricodecs::sfd::SfdAudioType::aix: return QStringLiteral("aix");
    case cricodecs::sfd::SfdAudioType::ac3: return QStringLiteral("ac3");
    case cricodecs::sfd::SfdAudioType::unknown: break;
    }
    return QStringLiteral("unknown");
}

QString video_type_name(cricodecs::sfd::SfdVideoType type) {
    switch (type) {
    case cricodecs::sfd::SfdVideoType::mpeg1: return QStringLiteral("mpeg1");
    case cricodecs::sfd::SfdVideoType::mpeg2: return QStringLiteral("mpeg2");
    case cricodecs::sfd::SfdVideoType::unknown: break;
    }
    return QStringLiteral("unknown");
}

QString header_variant_name(cricodecs::sfd::SfdHeaderVariant variant) {
    switch (variant) {
    case cricodecs::sfd::SfdHeaderVariant::sofdec_stream: return QStringLiteral("SofdecStream");
    case cricodecs::sfd::SfdHeaderVariant::sofdec_stream2: return QStringLiteral("SofdecStream2");
    case cricodecs::sfd::SfdHeaderVariant::unknown: break;
    }
    return QStringLiteral("unknown");
}

QString bytes_to_hex(std::span<const uint8_t> bytes) {
    QString out;
    out.reserve(static_cast<qsizetype>(bytes.size() * 2));
    for (const auto byte : bytes) {
        out += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
    }
    return out.toUpper();
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

QString version_tag_text(const cricodecs::sfd::SfdHeaderSummary& summary) {
    return bytes_to_hex(std::span<const uint8_t>(summary.version_tag_bytes.data(), summary.version_tag_size));
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    const cricodecs::sfd::SfdContainer& sfd
) {
    return sfd.save();
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::sfd::SfdContainer& sfd) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QStringLiteral("Streams"), QString::number(sfd.stream_count())});
    if (sfd.header_summary()) {
        const auto& summary = *sfd.header_summary();
        rows.push_back({
            QStringLiteral("Header metadata"),
            QStringLiteral("%1, %2 element(s)")
                .arg(header_variant_name(summary.variant))
                .arg(summary.element_count),
            11,
            -1
        });
        rows.push_back({QStringLiteral("Header variant"), header_variant_name(summary.variant)});
        rows.push_back({QStringLiteral("Header label"), utf8_to_qstring(summary.header_label)});
        rows.push_back({QStringLiteral("Version tag"), version_tag_text(summary)});
        rows.push_back({QStringLiteral("Pack size"), QString::number(summary.pack_size)});
        rows.push_back({QStringLiteral("Variable pack"), summary.variable_pack ? QStringLiteral("yes") : QStringLiteral("no")});
        rows.push_back({QStringLiteral("Min header packets"), QString::number(summary.min_header_packet_count)});
        rows.push_back({QStringLiteral("Reserved header size"), QString::number(summary.reserved_header_size)});
        rows.push_back({QStringLiteral("Elements"), QString::number(summary.element_count)});
        rows.push_back({QStringLiteral("Audio/video/private"), QStringLiteral("%1/%2/%3").arg(summary.audio_count).arg(summary.video_count).arg(summary.private_count)});
        rows.push_back({QStringLiteral("Bitrate B/s"), QString::number(static_cast<qulonglong>(summary.bitrate_bytes_per_second))});
        rows.push_back({QStringLiteral("Short output name"), utf8_to_qstring(summary.short_output_name)});
        rows.push_back({QStringLiteral("Output timestamp"), utf8_to_qstring(summary.output_timestamp)});
        rows.push_back({QStringLiteral("Output name"), utf8_to_qstring(summary.output_name)});
        rows.push_back({QStringLiteral("Builder version"), utf8_to_qstring(summary.builder_version)});
        for (size_t index = 0; index < summary.element_records.size(); ++index) {
            const auto& record = summary.element_records[index];
            rows.push_back({
                QStringLiteral("Header element %1").arg(static_cast<qulonglong>(index)),
                QStringLiteral("stream 0x%1, source %2, %3, %4")
                    .arg(record.stream_id, 2, 16, QLatin1Char('0')).toUpper()
                    .arg(record.source_type)
                    .arg(utf8_to_qstring(record.short_name))
                    .arg(utf8_to_qstring(record.timestamp)),
                11,
                static_cast<int>(index)
            });
        }
    }
    for (uint32_t index = 0; index < sfd.stream_count(); ++index) {
        const auto& stream = sfd.stream(index);
        rows.push_back({
            QStringLiteral("Stream %1").arg(index),
            QStringLiteral("%1 id 0x%2, type index %3, audio %4, video %5, packets %6, chunks %7, bytes %8")
                .arg(stream_type_name(stream.type))
                .arg(stream.stream_id, 2, 16, QLatin1Char('0')).toUpper()
                .arg(stream.type_index)
                .arg(audio_type_name(stream.audio_type))
                .arg(video_type_name(stream.video_type))
                .arg(stream.packet_count)
                .arg(static_cast<qulonglong>(stream.chunks.size()))
                .arg(static_cast<qulonglong>(stream.extracted_size)),
            4,
            static_cast<int>(index)
        });
        rows.push_back({
            QStringLiteral("Stream %1 detail").arg(index),
            QStringLiteral("%1 chunk(s), source %2")
                .arg(static_cast<qulonglong>(stream.chunks.size()))
                .arg(utf8_to_qstring(stream.source_name)),
            12,
            static_cast<int>(index)
        });
        if (stream.video_header) {
            rows.push_back({
                QStringLiteral("Stream %1 video").arg(index),
                QStringLiteral("%1x%2, aspect %3, framerate %4, bitrate %5")
                    .arg(stream.video_header->width)
                    .arg(stream.video_header->height)
                    .arg(stream.video_header->aspect_ratio_code)
                    .arg(stream.video_header->frame_rate_code)
                    .arg(stream.video_header->bit_rate_value)
            });
        }
    }
    return rows;
}

QString element_record_preview(const cricodecs::sfd::SfdElementRecord& record) {
    QStringList lines;
    lines.push_back(QStringLiteral("Stream id: 0x%1").arg(record.stream_id, 2, 16, QLatin1Char('0')).toUpper());
    lines.push_back(QStringLiteral("Source type: %1").arg(record.source_type));
    lines.push_back(QStringLiteral("Short name: %1").arg(utf8_to_qstring(record.short_name)));
    lines.push_back(QStringLiteral("Timestamp: %1").arg(utf8_to_qstring(record.timestamp)));
    if (record.picture_rate) {
        lines.push_back(QStringLiteral("Picture rate: %1").arg(*record.picture_rate));
    }
    if (record.width || record.height) {
        lines.push_back(QStringLiteral("Dimensions: %1x%2")
            .arg(record.width.value_or(0))
            .arg(record.height.value_or(0)));
    }
    if (record.frame_rate_code) {
        lines.push_back(QStringLiteral("Frame rate code: %1").arg(*record.frame_rate_code));
    }
    if (record.audio_channels) {
        lines.push_back(QStringLiteral("Audio channels: %1").arg(*record.audio_channels));
    }
    if (record.audio_sample_rate) {
        lines.push_back(QStringLiteral("Audio sample rate: %1").arg(*record.audio_sample_rate));
    }
    lines.push_back(QStringLiteral("Detail bytes: %1")
        .arg(bytes_to_hex(std::span<const uint8_t>(record.detail_bytes.data(), record.detail_bytes.size()))));
    lines.push_back(QStringLiteral("Footer bytes: %1")
        .arg(bytes_to_hex(std::span<const uint8_t>(record.footer_bytes.data(), record.footer_bytes.size()))));
    return lines.join(QLatin1Char('\n'));
}

QString header_summary_preview(const cricodecs::sfd::SfdHeaderSummary& summary) {
    QStringList lines;
    lines.push_back(QStringLiteral("Variant: %1").arg(header_variant_name(summary.variant)));
    lines.push_back(QStringLiteral("Header label: %1").arg(utf8_to_qstring(summary.header_label)));
    lines.push_back(QStringLiteral("Version tag: %1").arg(version_tag_text(summary)));
    lines.push_back(QStringLiteral("Pack size: %1").arg(summary.pack_size));
    lines.push_back(QStringLiteral("Variable pack: %1").arg(summary.variable_pack ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.push_back(QStringLiteral("Min header packets: %1").arg(summary.min_header_packet_count));
    lines.push_back(QStringLiteral("Reserved header size: %1").arg(summary.reserved_header_size));
    lines.push_back(QStringLiteral("Elements: %1").arg(summary.element_count));
    lines.push_back(QStringLiteral("Audio/video/private: %1/%2/%3")
        .arg(summary.audio_count)
        .arg(summary.video_count)
        .arg(summary.private_count));
    lines.push_back(QStringLiteral("Bitrate B/s: %1").arg(static_cast<qulonglong>(summary.bitrate_bytes_per_second)));
    lines.push_back(QStringLiteral("Short output name: %1").arg(utf8_to_qstring(summary.short_output_name)));
    lines.push_back(QStringLiteral("Output timestamp: %1").arg(utf8_to_qstring(summary.output_timestamp)));
    lines.push_back(QStringLiteral("Output name: %1").arg(utf8_to_qstring(summary.output_name)));
    lines.push_back(QStringLiteral("Builder version: %1").arg(utf8_to_qstring(summary.builder_version)));
    lines.push_back(QStringLiteral("Element record count: %1")
        .arg(static_cast<qulonglong>(summary.element_records.size())));
    return lines.join(QLatin1Char('\n'));
}

QString stream_detail_preview(const cricodecs::sfd::SfdStream& stream) {
    QStringList lines;
    lines.push_back(QStringLiteral("Index: %1").arg(stream.index));
    lines.push_back(QStringLiteral("Type: %1").arg(stream_type_name(stream.type)));
    lines.push_back(QStringLiteral("Type index: %1").arg(stream.type_index));
    lines.push_back(QStringLiteral("Stream id: 0x%1").arg(stream.stream_id, 2, 16, QLatin1Char('0')).toUpper());
    lines.push_back(QStringLiteral("Audio type: %1").arg(audio_type_name(stream.audio_type)));
    lines.push_back(QStringLiteral("Video type: %1").arg(video_type_name(stream.video_type)));
    lines.push_back(QStringLiteral("Source name: %1").arg(utf8_to_qstring(stream.source_name)));
    lines.push_back(QStringLiteral("Suggested path: %1").arg(path_to_qstring(stream.suggested_path())));
    lines.push_back(QStringLiteral("Packets: %1").arg(stream.packet_count));
    lines.push_back(QStringLiteral("Extracted size: %1").arg(static_cast<qulonglong>(stream.extracted_size)));
    if (stream.video_header) {
        const auto& video = *stream.video_header;
        lines.push_back(QStringLiteral("Video header: %1x%2, aspect %3, frame rate code %4, bitrate %5")
            .arg(video.width)
            .arg(video.height)
            .arg(video.aspect_ratio_code)
            .arg(video.frame_rate_code)
            .arg(video.bit_rate_value));
    }
    if (stream.element_record) {
        lines.push_back(QStringLiteral("Element record:"));
        lines.push_back(element_record_preview(*stream.element_record));
    }
    lines.push_back(QStringLiteral("Chunks: %1").arg(static_cast<qulonglong>(stream.chunks.size())));
    for (size_t index = 0; index < stream.chunks.size(); ++index) {
        const auto& chunk = stream.chunks[index];
        lines.push_back(QStringLiteral("[%1] source 0x%2, size %3")
            .arg(static_cast<qulonglong>(index))
            .arg(static_cast<qulonglong>(chunk.source_offset), 0, 16)
            .arg(chunk.size));
    }
    return lines.join(QLatin1Char('\n'));
}

std::expected<QString, QString> payload_preview(
    const cricodecs::sfd::SfdContainer& sfd,
    int payload_kind,
    int index
) {
    if (payload_kind == 4) {
        if (index < 0) {
            return std::unexpected(QStringLiteral("SFD stream preview failed: index out of range"));
        }
        auto data = sfd.extract_stream(static_cast<uint32_t>(index));
        if (!data) {
            return std::unexpected(QStringLiteral("SFD stream preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return hex_preview(std::span<const uint8_t>(data->data(), data->size()));
    }

    if (payload_kind == 11) {
        if (!sfd.header_summary()) {
            return std::unexpected(QStringLiteral("SFD header preview failed: no header summary"));
        }
        const auto& summary = *sfd.header_summary();
        if (index < 0) {
            return header_summary_preview(summary);
        }
        if (index >= static_cast<int>(summary.element_records.size())) {
            return std::unexpected(QStringLiteral("SFD header element preview failed: index out of range"));
        }
        return element_record_preview(summary.element_records[static_cast<size_t>(index)]);
    }

    if (payload_kind == 12) {
        if (index < 0 || index >= static_cast<int>(sfd.stream_count())) {
            return std::unexpected(QStringLiteral("SFD stream detail preview failed: index out of range"));
        }
        return stream_detail_preview(sfd.stream(static_cast<uint32_t>(index)));
    }

    return std::unexpected(QStringLiteral("SFD preview failed: unsupported payload kind"));
}

} // namespace cristudio::modules::sfd
