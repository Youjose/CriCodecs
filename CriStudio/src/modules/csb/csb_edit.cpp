#include "modules/csb/csb_edit.hpp"

#include "csb_container.hpp"
#include "modules/utf/utf_edit_ui.hpp"
#include "path_text.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <system_error>

namespace cristudio::modules::csb {
namespace {

void push_log(const BuildLogCallback& log, QString message) {
    if (log) {
        log(std::move(message));
    }
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

std::expected<void, QString> build_from_directory(
    DirectoryBuildConfig config,
    BuildLogCallback log
) {
    if (config.input_dir.empty()) {
        return std::unexpected(QStringLiteral("Choose a CSB source folder."));
    }
    if (config.output_path.empty()) {
        return std::unexpected(QStringLiteral("Choose an output path."));
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.input_dir, ec) || ec) {
        return std::unexpected(QStringLiteral("CSB source folder does not exist: %1").arg(path_to_qstring(config.input_dir)));
    }
    if (!std::filesystem::is_directory(config.input_dir, ec) || ec) {
        return std::unexpected(QStringLiteral("CSB source path is not a folder: %1").arg(path_to_qstring(config.input_dir)));
    }

    size_t file_count = 0;
    for (std::filesystem::recursive_directory_iterator it(config.input_dir, ec), end; it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (ec) {
            return std::unexpected(QStringLiteral("CSB source folder scan failed: %1").arg(utf8_to_qstring(ec.message())));
        }
        if (entry.is_regular_file(ec) && !ec) {
            ++file_count;
        }
    }
    if (ec) {
        return std::unexpected(QStringLiteral("CSB source folder scan failed: %1").arg(utf8_to_qstring(ec.message())));
    }
    if (file_count == 0) {
        return std::unexpected(QStringLiteral("CSB source folder contains no files."));
    }

    push_log(log, QStringLiteral("Building CSB from %1 file(s) under %2.")
        .arg(file_count)
        .arg(path_to_qstring(config.input_dir)));
    if (auto result = cricodecs::csb::CsbContainer::build_to_file(config.input_dir, config.output_path); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    push_log(log, QStringLiteral("CSB build wrote %1.").arg(path_to_qstring(config.output_path)));
    return {};
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    const cricodecs::csb::CsbContainer& csb
) {
    return csb.save();
}

std::vector<TransformDetailRow> build_job_detail_rows() {
    return {
        {QStringLiteral("Job"), QStringLiteral("Build a CSB cue/archive from a folder tree")},
        {QStringLiteral("Input"), QStringLiteral("directory of files, recursively scanned")},
        {QStringLiteral("Validation"), QStringLiteral("native CSB builder accepts supported payload files")},
        {QStringLiteral("Names"), QStringLiteral("relative paths become stream names")},
        {QStringLiteral("Output"), QStringLiteral(".csb")}
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::csb::CsbContainer& csb) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QStringLiteral("Header table"), utf8_to_qstring(std::string(csb.name())), 8, -1});
    rows.push_back({QStringLiteral("Sections"), QString::number(csb.section_count())});
    rows.push_back({QStringLiteral("Elements"), QString::number(csb.element_count())});
    rows.push_back({QStringLiteral("Embedded streams"), QString::number(csb.stream_count())});
    rows.push_back({QStringLiteral("Sound element table"), utf8_to_qstring(std::string(csb.sound_element_table().table_name())), 8, -2});
    for (uint32_t index = 0; index < csb.section_count(); ++index) {
        const auto& section = csb.section(index);
        rows.push_back({
            QStringLiteral("Section %1").arg(index),
            QStringLiteral("row %1, type %2, %3, size %4")
                .arg(section.row_index)
                .arg(section.table_type)
                .arg(utf8_to_qstring(section.name))
                .arg(section.data_size),
            8,
            static_cast<int>(index)
        });
    }
    for (uint32_t index = 0; index < csb.element_count(); ++index) {
        const auto& element = csb.element(index);
        rows.push_back({
            QStringLiteral("Element %1").arg(index),
            QStringLiteral("row %1, fmt %2, %3, wrapper %4, %5 ch @ %6 Hz, samples %7, streamed %8, wrapper bytes %9")
                .arg(element.row_index)
                .arg(element.format)
                .arg(utf8_to_qstring(element.name.empty() ? element.name_raw : element.name))
                .arg(utf8_to_qstring(element.wrapper_table_name))
                .arg(element.channels)
                .arg(element.sample_rate)
                .arg(element.sample_count)
                .arg(element.streamed ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(element.wrapper_size),
            10,
            static_cast<int>(index)
        });
    }
    for (uint32_t index = 0; index < csb.stream_count(); ++index) {
        const auto& stream = csb.stream(index);
        rows.push_back({
            QStringLiteral("Stream %1").arg(index),
            path_to_qstring(stream.suggested_path()),
            5,
            static_cast<int>(index)
        });
        rows.push_back({
            QStringLiteral("Stream %1 wrapper").arg(index),
            QStringLiteral("%1 UTF table, %2 bytes")
                .arg(utf8_to_qstring(stream.wrapper_table_name))
                .arg(stream.wrapper_size),
            9,
            static_cast<int>(index)
        });
    }
    return rows;
}

std::expected<QString, QString> payload_preview(
    const cricodecs::csb::CsbContainer& csb,
    int payload_kind,
    int index
) {
    if (payload_kind == 10) {
        if (index < 0 || index >= static_cast<int>(csb.element_count())) {
            return std::unexpected(QStringLiteral("CSB element preview failed: index out of range"));
        }
        const auto& element = csb.element(static_cast<uint32_t>(index));
        return QStringLiteral(
            "Name: %1\nFormat: %2\nWrapper: %3\nChannels: %4\nSample rate: %5\nSamples: %6\nStreamed: %7\nWrapper bytes: %8")
            .arg(utf8_to_qstring(element.name))
            .arg(element.format)
            .arg(utf8_to_qstring(element.wrapper_table_name))
            .arg(element.channels)
            .arg(element.sample_rate)
            .arg(element.sample_count)
            .arg(element.streamed ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(element.wrapper_size);
    }
    if (payload_kind == 5) {
        if (index < 0) {
            return std::unexpected(QStringLiteral("CSB stream preview failed: index out of range"));
        }
        auto data = csb.stream_data(static_cast<uint32_t>(index));
        if (!data) {
            return std::unexpected(QStringLiteral("CSB stream preview failed: %1").arg(utf8_to_qstring(data.error())));
        }
        return hex_preview(std::span<const uint8_t>(data->data(), data->size()));
    }

    if (payload_kind == 8) {
        if (index == -1) {
            return ::cristudio::modules::utf::utf_table_preview(csb.header_table());
        }
        if (index == -2) {
            return ::cristudio::modules::utf::utf_table_preview(csb.sound_element_table());
        }
        if (index < 0) {
            return std::unexpected(QStringLiteral("CSB section table preview failed: index out of range"));
        }
        auto table = csb.section_table(static_cast<uint32_t>(index));
        if (!table) {
            return std::unexpected(QStringLiteral("CSB section table preview failed: %1")
                .arg(utf8_to_qstring(table.error())));
        }
        return ::cristudio::modules::utf::utf_table_preview(*table);
    }

    if (payload_kind == 9) {
        if (index < 0) {
            return std::unexpected(QStringLiteral("CSB wrapper table preview failed: index out of range"));
        }
        auto table = csb.wrapper_table(static_cast<uint32_t>(index));
        if (!table) {
            return std::unexpected(QStringLiteral("CSB wrapper table preview failed: %1")
                .arg(utf8_to_qstring(table.error())));
        }
        return ::cristudio::modules::utf::utf_table_preview(*table);
    }

    return std::unexpected(QStringLiteral("CSB preview failed: unsupported payload kind"));
}

std::expected<cricodecs::utf::UtfTable, QString> payload_table(
    const cricodecs::csb::CsbContainer& csb,
    int payload_kind,
    int index
) {
    if (payload_kind == 8) {
        if (index == -1) {
            return csb.header_table();
        }
        if (index == -2) {
            return csb.sound_element_table();
        }
        if (index < 0) {
            return std::unexpected(QStringLiteral("CSB section table preview failed: index out of range"));
        }
        auto table = csb.section_table(static_cast<uint32_t>(index));
        if (!table) {
            return std::unexpected(QStringLiteral("CSB section table preview failed: %1")
                .arg(utf8_to_qstring(table.error())));
        }
        return *table;
    }

    if (payload_kind == 9) {
        if (index < 0) {
            return std::unexpected(QStringLiteral("CSB wrapper table preview failed: index out of range"));
        }
        auto table = csb.wrapper_table(static_cast<uint32_t>(index));
        if (!table) {
            return std::unexpected(QStringLiteral("CSB wrapper table preview failed: %1")
                .arg(utf8_to_qstring(table.error())));
        }
        return *table;
    }

    return std::unexpected(QStringLiteral("CSB table preview failed: unsupported payload kind"));
}

} // namespace cristudio::modules::csb
