/**
 * @file csb_builder.cpp
 * @brief CSB cue archive builder.
 *
 * Builder behavior follows vgmstream's CSB loader model where applicable, with
 * validation for the current C++23 rebuild path. Follow-up implementation by
 * Youjose.
 */

#include "csb_container.hpp"

#include <algorithm>
#include <fstream>

#include "csb_format.hpp"
#include "../aax/aax_container.hpp"
#include "../adx/adx_codec.hpp"
#include "../hca/hca_codec.hpp"
#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/io.hpp"
#include "../utilities/io_endian.hpp"

namespace cricodecs::csb {

namespace {

struct BuildStreamInfo {
    std::string name;
    std::string name_raw;
    uint8_t format = 0;
    std::string wrapper_table_name;
    uint8_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t sample_count = 0;
    bool looped = false;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> wrapper;
};

using detail::encode_cri_string_to_storage;

std::string normalize_archive_name(const std::filesystem::path& path) {
    std::filesystem::path normalized = path.lexically_normal();
    normalized.replace_extension();

    std::string name = normalized.generic_string();
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }

    return name;
}

std::expected<std::vector<uint8_t>, std::string> build_hca_wrapper(std::span<const uint8_t> payload, bool looped) {
    utf::UtfTable wrapper = utf::UtfTable::create("HCA");
    wrapper.add_column("data", utf::ColumnType::VLData);
    wrapper.add_column("lpflg", utf::ColumnType::UInt8);

    const uint32_t row = wrapper.add_row();
    wrapper.set(row, "data", std::vector<uint8_t>(payload.begin(), payload.end()));
    wrapper.set(row, "lpflg", static_cast<uint8_t>(looped ? 1 : 0));

    return wrapper.build();
}

std::expected<std::vector<uint8_t>, std::string> build_aax_wrapper(std::span<const uint8_t> payload, bool looped) {
    utf::UtfTable wrapper = utf::UtfTable::create("AAX");
    wrapper.add_column("data", utf::ColumnType::VLData);
    wrapper.add_column("lpflg", utf::ColumnType::UInt8);

    const uint32_t row = wrapper.add_row();
    wrapper.set(row, "data", std::vector<uint8_t>(payload.begin(), payload.end()));
    wrapper.set(row, "lpflg", static_cast<uint8_t>(looped ? 1 : 0));

    return wrapper.build();
}

std::expected<std::vector<uint8_t>, std::string> build_ahx_wrapper(std::span<const uint8_t> payload) {
    utf::UtfTable wrapper = utf::UtfTable::create("AHX");
    wrapper.add_column("data", utf::ColumnType::VLData);

    const uint32_t row = wrapper.add_row();
    wrapper.set(row, "data", std::vector<uint8_t>(payload.begin(), payload.end()));

    return wrapper.build();
}

std::expected<BuildStreamInfo, std::string> inspect_stream_payload(
    std::string name,
    std::string name_raw,
    std::vector<uint8_t> payload
) {
    if (payload.empty()) {
        return std::unexpected("CSB build failed: input stream payload is empty");
    }

    BuildStreamInfo info;
    info.name = std::move(name);
    info.name_raw = std::move(name_raw);
    info.payload = std::move(payload);

    if (info.payload.size() >= 4 &&
        info.payload[0] == '@' &&
        info.payload[1] == 'U' &&
        info.payload[2] == 'T' &&
        info.payload[3] == 'F') {
        auto aax_wrapper = aax::AaxContainer::load(info.payload);
        if (aax_wrapper) {
            info.format = 0;
            info.wrapper_table_name = "AAX";
            info.channels = aax_wrapper->channels();
            info.sample_rate = aax_wrapper->sample_rate();
            info.sample_count = aax_wrapper->sample_count();
            info.looped = aax_wrapper->has_loop_segments();
            info.wrapper = std::move(info.payload);
            info.payload.clear();
            return info;
        }
    }

    if (info.payload.size() >= 4 &&
        info.payload[0] == 'H' &&
        info.payload[1] == 'C' &&
        info.payload[2] == 'A' &&
        info.payload[3] == '\0') {
        auto hca = hca::Hca::load(info.payload);
        if (!hca) {
            return std::unexpected("CSB build failed: unsupported HCA payload: could not parse HCA header");
        }
        const auto& hca_info = hca->header();

        info.format = 6;
        info.wrapper_table_name = "HCA";
        info.channels = hca_info.fmt.channel_count;
        info.sample_rate = hca_info.fmt.sample_rate;
        info.sample_count = hca_info.sample_count();
        info.looped = hca_info.loop.enabled();
        return info;
    }

    if (info.payload.size() >= 0x14 && io::read_be<uint16_t>(info.payload.data()) == 0x8000) {
        const uint8_t encoding_type = info.payload[0x04];
        if (encoding_type == 0x10 || encoding_type == 0x11) {
            if (info.payload[0x05] != 0 || info.payload[0x06] != 0 || info.payload[0x12] != 0x06) {
                return std::unexpected("CSB build failed: unsupported AHX payload header");
            }

            info.format = 2;
            info.wrapper_table_name = "AHX";
            info.channels = info.payload[0x07];
            info.sample_rate = io::read_be<uint32_t>(info.payload.data() + 0x08);
            info.sample_count = io::read_be<uint32_t>(info.payload.data() + 0x0C);
            info.looped = false;
            return info;
        }

        adx::AdxDecoder decoder;
        auto load_result = decoder.load(info.payload);
        if (!load_result) {
            return std::unexpected("CSB build failed: unsupported ADX/AAX payload: " + load_result.error());
        }

        const auto& header = decoder.header();
        info.format = 0;
        info.wrapper_table_name = "AAX";
        info.channels = header.channels;
        info.sample_rate = header.sample_rate;
        info.sample_count = header.sample_count;
        info.looped = decoder.has_loops();
        return info;
    }

    return std::unexpected("CSB build failed: unsupported stream payload type");
}

std::expected<std::vector<uint8_t>, std::string> build_wrapper_for_stream(const BuildStreamInfo& info) {
    if (!info.wrapper.empty()) {
        return info.wrapper;
    }

    switch (info.format) {
        case 0:
            return build_aax_wrapper(info.payload, info.looped);
        case 6:
            return build_hca_wrapper(info.payload, info.looped);
        case 2:
            return build_ahx_wrapper(info.payload);
        default:
            return std::unexpected("CSB build failed: unsupported stream format");
    }
}

std::expected<std::vector<BuildStreamInfo>, std::string> inspect_build_entries(
    std::span<const CsbBuildEntry> entries,
    const text::EncodingOptions& encoding
) {
    if (entries.empty()) {
        return std::unexpected("CSB build failed: no input entries were provided");
    }

    std::vector<BuildStreamInfo> streams;
    streams.reserve(entries.size());

    util::flat_unordered_set<std::string> seen_names;
    seen_names.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.source_path.empty()) {
            return std::unexpected("CSB build entry has an empty source path");
        }

        const std::filesystem::path archive_path =
            entry.archive_path.empty() ? entry.source_path.filename() : entry.archive_path;
        const std::string archive_name = normalize_archive_name(archive_path);
        if (archive_name.empty()) {
            return std::unexpected("CSB build entry produced an empty archive path");
        }
        auto archive_name_raw = encode_cri_string_to_storage(
            archive_name,
            encoding,
            "CSB archive path encode failed"
        );
        if (!archive_name_raw) {
            return std::unexpected(archive_name_raw.error());
        }

        if (!seen_names.emplace(archive_name).second) {
            return std::unexpected("CSB build failed: duplicate archive path: " + archive_name);
        }

        auto payload = io::read_file_bytes(entry.source_path, "CSB build failed");
        if (!payload) {
            return std::unexpected(payload.error());
        }

        auto stream_info = inspect_stream_payload(archive_name, std::move(*archive_name_raw), std::move(*payload));
        if (!stream_info) {
            return std::unexpected("CSB build failed: could not inspect '" + archive_name + "': " + stream_info.error());
        }

        streams.push_back(std::move(*stream_info));
    }

    return streams;
}

std::expected<std::vector<uint8_t>, std::string> build_minimal_csb(const std::vector<BuildStreamInfo>& streams) {
    utf::UtfTable sound_element = utf::UtfTable::create("TBLSDL");
    sound_element.add_column("name", utf::ColumnType::String);
    sound_element.add_column("data", utf::ColumnType::VLData);
    sound_element.add_column("fmt", utf::ColumnType::UInt8);
    sound_element.add_column("nch", utf::ColumnType::UInt8);
    sound_element.add_column("stmflg", utf::ColumnType::UInt8);
    sound_element.add_column("sfreq", utf::ColumnType::UInt32);
    sound_element.add_column("nsmpl", utf::ColumnType::UInt32);

    for (const auto& stream : streams) {
        auto wrapper = build_wrapper_for_stream(stream);
        if (!wrapper) {
            return std::unexpected("CSB build failed: could not build wrapper for '" + stream.name + "': " + wrapper.error());
        }

        const uint32_t row = sound_element.add_row();
        sound_element.set(row, "name", stream.name_raw.empty() ? stream.name : stream.name_raw);
        sound_element.set(row, "data", std::move(*wrapper));
        sound_element.set(row, "fmt", stream.format);
        sound_element.set(row, "nch", stream.channels);
        sound_element.set(row, "stmflg", static_cast<uint8_t>(0));
        sound_element.set(row, "sfreq", stream.sample_rate);
        sound_element.set(row, "nsmpl", stream.sample_count);
    }

    std::vector<uint8_t> sound_element_bytes = sound_element.build();

    utf::UtfTable root = utf::UtfTable::create("TBLCSB");
    root.add_column("name", utf::ColumnType::String);
    root.add_column("ttype", utf::ColumnType::UInt8);
    root.add_column("utf", utf::ColumnType::VLData);

    const uint32_t row = root.add_row();
    root.set(row, "name", std::string("SOUND_ELEMENT"));
    root.set(row, "ttype", static_cast<uint8_t>(4));
    root.set(row, "utf", std::move(sound_element_bytes));

    return root.build();
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> CsbContainer::build(
    std::span<const CsbBuildEntry> entries,
    const text::EncodingOptions& encoding
) {
    auto streams = inspect_build_entries(entries, encoding);
    if (!streams) {
        return std::unexpected(streams.error());
    }

    return build_minimal_csb(*streams);
}

std::expected<std::vector<uint8_t>, std::string> CsbContainer::build_from_directory(
    const std::filesystem::path& input_dir,
    const text::EncodingOptions& encoding
) {
    std::error_code ec;
    if (!std::filesystem::exists(input_dir, ec) || ec) {
        return std::unexpected("CSB input directory does not exist: " + input_dir.string());
    }
    if (!std::filesystem::is_directory(input_dir, ec) || ec) {
        return std::unexpected("CSB input path is not a directory: " + input_dir.string());
    }

    std::vector<CsbBuildEntry> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto relative = std::filesystem::relative(entry.path(), input_dir, ec);
        if (ec) {
            return std::unexpected("CSB build failed: could not compute relative path for " + entry.path().string());
        }

        entries.push_back({entry.path(), relative});
    }

    if (entries.empty()) {
        return std::unexpected("CSB input directory contains no files");
    }

    std::sort(entries.begin(), entries.end(), [](const CsbBuildEntry& lhs, const CsbBuildEntry& rhs) {
        return normalize_archive_name(lhs.archive_path) < normalize_archive_name(rhs.archive_path);
    });

    return build(entries, encoding);
}

std::expected<void, std::string> CsbContainer::build_to_file(
    std::span<const CsbBuildEntry> entries,
    const std::filesystem::path& output_path,
    const text::EncodingOptions& encoding
) {
    auto bytes = build(entries, encoding);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return std::unexpected("CSB build failed: could not open output file: " + output_path.string());
    }

    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!output.good()) {
        return std::unexpected("CSB build failed: could not write output file: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> CsbContainer::build_to_file(
    const std::filesystem::path& input_dir,
    const std::filesystem::path& output_path,
    const text::EncodingOptions& encoding
) {
    auto bytes = build_from_directory(input_dir, encoding);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return std::unexpected("CSB build failed: could not open output file: " + output_path.string());
    }

    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!output.good()) {
        return std::unexpected("CSB build failed: could not write output file: " + output_path.string());
    }

    return {};
}

} // namespace cricodecs::csb
