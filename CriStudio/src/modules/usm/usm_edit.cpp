#include "modules/usm/usm_edit.hpp"

#include "modules/utf/utf_edit_ui.hpp"
#include "path_text.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>
#include <span>
#include <string>

namespace cristudio::modules::usm {
namespace {

constexpr size_t kMaxStreamRows = 32;

QString fourcc_text(uint32_t value) {
    char chars[5] = {
        static_cast<char>((value >> 24) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>(value & 0xFF),
        '\0'
    };
    return QString::fromLatin1(chars, 4);
}

QString size_text(uint64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    const double value = static_cast<double>(bytes);
    if (value >= mib) {
        return QStringLiteral("%1 MiB").arg(value / mib, 0, 'f', value >= 100.0 * mib ? 0 : 1);
    }
    if (value >= kib) {
        return QStringLiteral("%1 KiB").arg(value / kib, 0, 'f', value >= 100.0 * kib ? 0 : 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString payload_type_text(cricodecs::usm::UsmPayloadType type) {
    switch (type) {
    case cricodecs::usm::UsmPayloadType::Stream:
        return QStringLiteral("stream");
    case cricodecs::usm::UsmPayloadType::Header:
        return QStringLiteral("header");
    case cricodecs::usm::UsmPayloadType::SectionEnd:
        return QStringLiteral("section-end");
    case cricodecs::usm::UsmPayloadType::Metadata:
        return QStringLiteral("metadata");
    }
    return QStringLiteral("unknown");
}

QString compact_stream_summary(const cricodecs::usm::UsmStreamInfo& stream) {
    const auto name = utf8_to_qstring(stream.filename.empty() ? stream.filename_raw : stream.filename);
    return QStringLiteral("%1 ch %2, %3, %4, avbps %5")
        .arg(fourcc_text(static_cast<uint32_t>(stream.stream_id)))
        .arg(stream.channel_no)
        .arg(name)
        .arg(size_text(stream.filesize))
        .arg(stream.avbps);
}

QString hex_preview(std::span<const uint8_t> bytes, size_t max_bytes = 4096) {
    const auto count = std::min(bytes.size(), max_bytes);
    QString out;
    out.reserve(static_cast<qsizetype>(count * 5 + 128));
    for (size_t offset = 0; offset < count; offset += 16) {
        const auto row_end = std::min(offset + 16, count);
        out += QStringLiteral("%1  |  ").arg(static_cast<qulonglong>(offset), 8, 16, QLatin1Char('0')).toUpper();
        for (size_t index = offset; index < offset + 16; ++index) {
            out += index < row_end
                ? QStringLiteral("%1 ").arg(bytes[index], 2, 16, QLatin1Char('0')).toUpper()
                : QStringLiteral("   ");
        }
        out += QStringLiteral(" | ");
        for (size_t index = offset; index < row_end; ++index) {
            const auto ch = bytes[index];
            out += (ch >= 0x20 && ch <= 0x7E) ? QLatin1Char(static_cast<char>(ch)) : QLatin1Char('.');
        }
        out += QLatin1Char('\n');
    }
    if (bytes.size() > count) {
        out += QStringLiteral("\n... %1 more bytes").arg(static_cast<qulonglong>(bytes.size() - count));
    }
    return out;
}

QString compact_chunk_summary(const cricodecs::usm::UsmChunk& chunk, uint64_t file_offset) {
    return QStringLiteral("offset 0x%1, %2, payload %3, chunk %4, padding %5%6")
        .arg(static_cast<qulonglong>(file_offset), 8, 16, QLatin1Char('0')).toUpper()
        .arg(payload_type_text(chunk.payload_type()))
        .arg(size_text(chunk.payload.size()))
        .arg(size_text(chunk.header.chunk_size))
        .arg(size_text(chunk.header.padding))
        .arg(chunk.is_utf_payload() ? QStringLiteral(", UTF") : QString{});
}

} // namespace

std::vector<TransformDetailRow> detail_rows(cricodecs::usm::UsmReader& usm) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QStringLiteral("Container filename"), utf8_to_qstring(std::string(usm.container_filename()))});
    rows.push_back({QStringLiteral("CRID table"), utf8_to_qstring(std::string(usm.crid_header().table_name())), 10, -1});
    rows.push_back({QStringLiteral("Streams"), QString::number(static_cast<qsizetype>(usm.streams().size()))});
    rows.push_back({QStringLiteral("Chunks"), QString::number(static_cast<qsizetype>(usm.chunks().size()))});
    if (usm.sfsh_header()) {
        const auto& sfsh = *usm.sfsh_header();
        rows.push_back({QStringLiteral("SFSH version"), QString::number(sfsh.version)});
        rows.push_back({QStringLiteral("SFSH payload size"), QString::number(sfsh.payload_size)});
        rows.push_back({QStringLiteral("SFSH codec marker"), QString::number(sfsh.codec_marker())});
        rows.push_back({QStringLiteral("SFSH normalized codec"), QString::number(sfsh.normalized_codec_marker())});
    }

    struct ChunkGroupSummary {
        size_t chunk_count = 0;
        uint64_t payload_bytes = 0;
    };

    size_t stream_chunk_count = 0;
    size_t utf_chunk_count = 0;
    size_t metadata_chunk_count = 0;
    size_t header_chunk_count = 0;
    size_t section_end_chunk_count = 0;
    std::map<std::pair<uint32_t, uint8_t>, ChunkGroupSummary> chunk_groups;
    for (const auto& chunk : usm.chunks()) {
        switch (chunk.payload_type()) {
        case cricodecs::usm::UsmPayloadType::Stream:
            ++stream_chunk_count;
            break;
        case cricodecs::usm::UsmPayloadType::Header:
            ++header_chunk_count;
            break;
        case cricodecs::usm::UsmPayloadType::SectionEnd:
            ++section_end_chunk_count;
            break;
        case cricodecs::usm::UsmPayloadType::Metadata:
            ++metadata_chunk_count;
            break;
        }
        if (chunk.is_utf_payload()) {
            ++utf_chunk_count;
        }
        auto& group = chunk_groups[{chunk.header.magic, chunk.header.channel_no}];
        ++group.chunk_count;
        group.payload_bytes += chunk.payload.size();
    }

    rows.push_back({QStringLiteral("Stream payload chunks"), QString::number(static_cast<qulonglong>(stream_chunk_count))});
    rows.push_back({QStringLiteral("UTF payload chunks"), QString::number(static_cast<qulonglong>(utf_chunk_count))});
    rows.push_back({QStringLiteral("Metadata chunks"), QString::number(static_cast<qulonglong>(metadata_chunk_count))});
    rows.push_back({QStringLiteral("Header chunks"), QString::number(static_cast<qulonglong>(header_chunk_count))});
    rows.push_back({QStringLiteral("Section-end chunks"), QString::number(static_cast<qulonglong>(section_end_chunk_count))});
    for (const auto& [key, group] : chunk_groups) {
        rows.push_back({
            QStringLiteral("Chunk group %1 ch %2")
                .arg(fourcc_text(key.first))
                .arg(key.second),
            QStringLiteral("%1 chunk(s), %2 payload")
                .arg(static_cast<qulonglong>(group.chunk_count))
                .arg(size_text(group.payload_bytes))
        });
    }

    const size_t shown_streams = std::min(usm.streams().size(), kMaxStreamRows);
    for (size_t index = 0; index < shown_streams; ++index) {
        const auto& stream = usm.streams()[index];
        rows.push_back({
            QStringLiteral("Stream %1").arg(static_cast<qulonglong>(index)),
            compact_stream_summary(stream),
            13,
            static_cast<int>(index)
        });
    }
    if (usm.streams().size() > shown_streams) {
        rows.push_back({
            QStringLiteral("Additional streams"),
            QStringLiteral("%1 more stream row(s) omitted from editor table")
                .arg(static_cast<qulonglong>(usm.streams().size() - shown_streams))
        });
    }
    return rows;
}

TransformDetailRow chunk_detail_row(const cricodecs::usm::UsmReader& usm, size_t index, uint64_t file_offset) {
    if (index >= usm.chunks().size()) {
        return {};
    }
    const auto& chunk = usm.chunks()[index];
    const auto magic = fourcc_text(chunk.header.magic);
    return {
        QStringLiteral("Chunk %1 %2 ch %3")
            .arg(static_cast<qulonglong>(index))
            .arg(magic)
            .arg(chunk.header.channel_no),
        compact_chunk_summary(chunk, file_offset),
        chunk.is_utf_payload() ? 10 : 3,
        static_cast<int>(index)
    };
}

std::vector<TransformDetailRow> chunk_detail_rows(const cricodecs::usm::UsmReader& usm) {
    std::vector<TransformDetailRow> rows;
    rows.reserve(usm.chunks().size());
    uint64_t file_offset = 0;
    for (size_t index = 0; index < usm.chunks().size(); ++index) {
        rows.push_back(chunk_detail_row(usm, index, file_offset));
        file_offset += 8ull + usm.chunks()[index].header.chunk_size;
    }
    return rows;
}

TransformDetailRow chunk_detail_row(const cricodecs::usm::UsmReader& usm, size_t index) {
    uint64_t file_offset = 0;
    for (size_t i = 0; i < index && i < usm.chunks().size(); ++i) {
        file_offset += 8ull + usm.chunks()[i].header.chunk_size;
    }
    return chunk_detail_row(usm, index, file_offset);
}

std::expected<QString, QString> chunk_payload_preview(
    const cricodecs::usm::UsmReader& usm,
    int index
) {
    auto sample = chunk_payload_sample(usm, index);
    if (!sample) {
        return std::unexpected(sample.error());
    }
    return hex_preview(*sample);
}

std::expected<std::vector<uint8_t>, QString> chunk_payload_sample(
    const cricodecs::usm::UsmReader& usm,
    int index,
    size_t max_bytes
) {
    if (index < 0 || index >= static_cast<int>(usm.chunks().size())) {
        return std::unexpected(QStringLiteral("USM chunk preview failed: index out of range"));
    }
    const auto& chunk = usm.chunks()[static_cast<size_t>(index)];
    const auto count = std::min(chunk.payload.size(), max_bytes);
    return std::vector<uint8_t>(chunk.payload.begin(), chunk.payload.begin() + static_cast<std::ptrdiff_t>(count));
}

std::expected<QString, QString> utf_payload_preview(
    cricodecs::usm::UsmReader& usm,
    int index
) {
    auto table = utf_payload_table(usm, index);
    if (!table) {
        return std::unexpected(table.error());
    }
    return ::cristudio::modules::utf::utf_table_preview(*table);
}

std::expected<cricodecs::utf::UtfTable, QString> utf_payload_table(
    cricodecs::usm::UsmReader& usm,
    int index
) {
    if (index < 0) {
        return usm.crid_header();
    }
    if (index >= static_cast<int>(usm.chunks().size())) {
        return std::unexpected(QStringLiteral("USM UTF chunk preview failed: index out of range"));
    }

    const auto& chunk = usm.chunks()[static_cast<size_t>(index)];
    auto table = chunk.load_utf_payload();
    if (!table) {
        return std::unexpected(QStringLiteral("USM UTF chunk preview failed: %1")
            .arg(utf8_to_qstring(table.error())));
    }
    return *table;
}

std::expected<QString, QString> stream_payload_preview(
    cricodecs::usm::UsmReader& usm,
    int index
) {
    auto preview = stream_payload_sample(usm, index);
    if (!preview) {
        return std::unexpected(preview.error());
    }
    const auto& stream = usm.streams()[static_cast<size_t>(index)];

    return QStringLiteral("USM stream %1 payload sample\nDeclared bytes: %2\nShown bytes: %3\n\n%4")
        .arg(index)
        .arg(static_cast<qulonglong>(stream.filesize))
        .arg(static_cast<qulonglong>(preview->size()))
        .arg(hex_preview(*preview));
}

std::expected<std::vector<uint8_t>, QString> stream_payload_sample(
    cricodecs::usm::UsmReader& usm,
    int index,
    size_t max_bytes
) {
    if (index < 0 || index >= static_cast<int>(usm.streams().size())) {
        return std::unexpected(QStringLiteral("USM stream preview failed: index out of range"));
    }

    auto preview = usm.extract_stream_sample(static_cast<uint32_t>(index), max_bytes);
    if (!preview) {
        return std::unexpected(QStringLiteral("USM stream preview failed: %1")
            .arg(utf8_to_qstring(preview.error())));
    }
    return *preview;
}

} // namespace cristudio::modules::usm
