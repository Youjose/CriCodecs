/**
 * @file sfd_builder.cpp
 * @brief SofDec 1/SFD builder.
 *
 * Builder behavior is based on official SofDec tooling evidence, especially
 * `sfdmuxg` and the fixed-pack SofdecStream builder path documented.
 * C++23 implementation by Youjose.
 */

#include "sfd_container.hpp"

#include "../adx/adx_codec.hpp"
#include "../utilities/io.hpp"
#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/string.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace cricodecs::sfd {

namespace {

using io::write_le;
using io::append_be;
using util::lowercase_ascii;
using util::uppercase_ascii;

constexpr uint8_t packet_program_end = 0xB9;
constexpr uint8_t packet_system_header = 0xBB;
constexpr uint8_t packet_private_stream_1 = 0xBD;
constexpr uint8_t packet_padding_stream = 0xBE;
constexpr uint8_t packet_private_stream_2 = 0xBF;
constexpr uint8_t packet_audio_stream_0 = 0xC0;
constexpr uint8_t packet_video_stream_0 = 0xE0;
constexpr uint32_t sector_size = 2048;
constexpr uint32_t pack_header_size = 12;
constexpr uint32_t packet_header_size = 6;
constexpr uint32_t sofdec_header_payload_size = sector_size - pack_header_size - packet_header_size;

struct HeaderLayout {
    uint32_t label_offset = 0;
    uint32_t version_offset = 0;
    uint32_t builder_version_offset = 0;
    uint32_t builder_version_size = 0;
    uint32_t stream_counts_offset = 0;
    uint32_t bitrate_offset = 0;
    uint32_t output_name_offset = 0;
    uint32_t short_output_name_offset = 0;
    uint32_t output_timestamp_offset = 0;
    uint32_t pack_size_offset = 0;
    uint32_t min_header_packets_offset = 0;
    uint32_t reserved_header_size_offset = 0;
    uint32_t element_table_offset = 0;
    uint32_t element_record_size = 0;
    bool has_standard_element_table = false;
};

constexpr HeaderLayout sofdec_stream_layout{
    .label_offset = 32,
    .version_offset = 56,
    .builder_version_offset = 96,
    .builder_version_size = 32,
    .stream_counts_offset = 176,
    .bitrate_offset = 180,
    .output_name_offset = 224,
    .short_output_name_offset = 64,
    .output_timestamp_offset = 76,
    .pack_size_offset = 128,
    .min_header_packets_offset = 136,
    .reserved_header_size_offset = 140,
    .element_table_offset = 384,
    .element_record_size = 64,
    .has_standard_element_table = true,
};

constexpr HeaderLayout sofdec_stream2_layout{
    .label_offset = 14,
    .version_offset = 38,
    .builder_version_offset = 46,
    .builder_version_size = 64,
    .stream_counts_offset = 174,
    .bitrate_offset = 0,
    .output_name_offset = 110,
    .short_output_name_offset = 0,
    .output_timestamp_offset = 0,
    .pack_size_offset = 0,
    .min_header_packets_offset = 0,
    .reserved_header_size_offset = 0,
    .element_table_offset = 0,
    .element_record_size = 0,
    .has_standard_element_table = false,
};

struct MuxProfileDescriptor {
    SfdBuildProfile build_profile = SfdBuildProfile::sofdec_stream_standard_fixed_2048;
    SfdHeaderVariant variant = SfdHeaderVariant::unknown;
    HeaderLayout layout{};
    std::string_view label;
    std::array<uint8_t, 4> version_tag{};
    uint8_t version_tag_size = 0;
    std::string_view default_builder_version;
};

constexpr MuxProfileDescriptor sofdec_stream_standard_fixed_2048_profile{
    .build_profile = SfdBuildProfile::sofdec_stream_standard_fixed_2048,
    .variant = SfdHeaderVariant::sofdec_stream,
    .layout = sofdec_stream_layout,
    .label = "SofdecStream            ",
    .version_tag = {2, 17, 0, 0},
    .version_tag_size = 2,
    .default_builder_version = "CriCodecs SFD Builder         ",
};

constexpr MuxProfileDescriptor sofdec_stream2_v23249_profile{
    .build_profile = SfdBuildProfile::sofdec_stream2_fixed_2048_v23249,
    .variant = SfdHeaderVariant::sofdec_stream2,
    .layout = sofdec_stream2_layout,
    .label = "SofdecStream2           ",
    .version_tag = {2, 3, 2, 73},
    .version_tag_size = 4,
    .default_builder_version = "CriCodecs SofdecStream2 v23249-compatible",
};

constexpr MuxProfileDescriptor sofdec_stream2_v23310_profile{
    .build_profile = SfdBuildProfile::sofdec_stream2_fixed_2048_v23310,
    .variant = SfdHeaderVariant::sofdec_stream2,
    .layout = sofdec_stream2_layout,
    .label = "SofdecStream2           ",
    .version_tag = {2, 3, 3, 10},
    .version_tag_size = 4,
    .default_builder_version = "CriCodecs SofdecStream2 v23310-compatible",
};

struct TimedUnit {
    size_t offset = 0;
    size_t size = 0;
    double time_seconds = 0.0;
};

struct VideoSource {
    std::vector<uint8_t> bytes;
    SfdVideoType video_type = SfdVideoType::unknown;
    uint8_t source_type = 1;
    SfdVideoSequenceHeader sequence_header;
    uint32_t fps_n = 30000;
    uint32_t fps_d = 1001;
    double duration_seconds = 0.0;
    std::string source_name;
    std::vector<TimedUnit> units;
};

struct AudioSource {
    std::vector<uint8_t> bytes;
    adx::AdxHeader header{};
    SfdAudioType audio_type = SfdAudioType::unknown;
    uint32_t samples_per_block = 0;
    size_t data_start = 0;
    double duration_seconds = 0.0;
    std::string source_name;
    std::vector<TimedUnit> units;
};

struct StreamCursor {
    const std::vector<uint8_t>* bytes = nullptr;
    const std::vector<TimedUnit>* units = nullptr;
    size_t unit_index = 0;
    size_t unit_offset = 0;
    uint8_t stream_id = 0;
    bool is_audio = false;

    [[nodiscard]] bool done() const noexcept {
        return bytes == nullptr || units == nullptr || unit_index >= units->size();
    }

    [[nodiscard]] double next_time() const noexcept {
        return done() ? std::numeric_limits<double>::infinity() : (*units)[unit_index].time_seconds;
    }

    [[nodiscard]] size_t remaining_in_unit() const noexcept {
        if (done()) {
            return 0;
        }
        const auto& unit = (*units)[unit_index];
        return unit.size > unit_offset ? unit.size - unit_offset : 0;
    }

    [[nodiscard]] size_t absolute_offset() const noexcept {
        const auto& unit = (*units)[unit_index];
        return unit.offset + unit_offset;
    }

    void advance(size_t bytes_to_consume) noexcept {
        if (done()) {
            return;
        }

        unit_offset += bytes_to_consume;
        while (!done()) {
            const auto& unit = (*units)[unit_index];
            if (unit_offset < unit.size) {
                break;
            }
            ++unit_index;
            unit_offset = 0;
        }
    }
};

struct SectorPatch {
    size_t offset = 0;
    uint64_t scr = 0;
    bool has_system_header = false;
};

[[nodiscard]] const MuxProfileDescriptor& resolve_build_profile_descriptor(SfdBuildProfile build_profile) {
    switch (build_profile) {
        case SfdBuildProfile::sofdec_stream2_fixed_2048_v23249:
            return sofdec_stream2_v23249_profile;
        case SfdBuildProfile::sofdec_stream2_fixed_2048_v23310:
            return sofdec_stream2_v23310_profile;
        case SfdBuildProfile::sofdec_stream_standard_fixed_2048:
        default:
            return sofdec_stream_standard_fixed_2048_profile;
    }
}

[[nodiscard]] std::expected<SfdBuildProfile, std::string> resolve_build_profile(const SfdBuildInput& input) {
    if (input.build_profile.has_value() && input.mux_profile.has_value() &&
        *input.build_profile != *input.mux_profile) {
        return std::unexpected("SFD build failed: canonical build_profile and compatibility mux_profile differ");
    }

    if (input.build_profile.has_value()) {
        return *input.build_profile;
    }
    if (input.mux_profile.has_value()) {
        return *input.mux_profile;
    }
    return SfdBuildProfile::sofdec_stream_standard_fixed_2048;
}

[[nodiscard]] std::expected<std::string_view, std::string> resolve_header_builder_version(
    const SfdBuildInput& input,
    const MuxProfileDescriptor& profile
) {
    if (!input.header_builder_version.empty() &&
        !input.header_builder_version_override.empty() &&
        input.header_builder_version != input.header_builder_version_override) {
        return std::unexpected(
            "Conflicting SFD header builder versions: canonical header_builder_version and compatibility "
            "header_builder_version_override differ"
        );
    }

    if (!input.header_builder_version.empty()) {
        return std::string_view(input.header_builder_version);
    }
    if (!input.header_builder_version_override.empty()) {
        return std::string_view(input.header_builder_version_override);
    }
    return profile.default_builder_version;
}

[[nodiscard]] std::expected<std::string_view, std::string> resolve_source_name(
    std::string_view canonical_source_name,
    std::string_view compatibility_stream_name,
    std::string_view fallback_source_name,
    std::string_view source_kind
) {
    if (!canonical_source_name.empty() &&
        !compatibility_stream_name.empty() &&
        canonical_source_name != compatibility_stream_name) {
        return std::unexpected(
            "Conflicting SFD " + std::string(source_kind) +
            " source names: canonical " + std::string(source_kind) +
            "_source_name and compatibility " + std::string(source_kind) +
            "_stream_name differ"
        );
    }

    if (!canonical_source_name.empty()) {
        return canonical_source_name;
    }
    if (!compatibility_stream_name.empty()) {
        return compatibility_stream_name;
    }
    return fallback_source_name;
}

[[nodiscard]] std::string replace_extension(const std::filesystem::path& path, std::string_view extension) {
    std::string stem = path.stem().string();
    if (stem.empty()) {
        stem = path.filename().string();
    }
    stem += extension;
    return stem;
}

[[nodiscard]] std::array<uint8_t, 12> make_short_name(std::string_view source_name) {
    constexpr size_t short_name_stem_size = 8;
    constexpr size_t short_name_ext_size = 4;

    std::array<uint8_t, short_name_stem_size + short_name_ext_size> name{};
    name.fill(static_cast<uint8_t>(' '));

    std::filesystem::path path(source_name);
    std::string stem = uppercase_ascii(path.stem().string());
    std::string ext = uppercase_ascii(path.extension().string());

    const size_t stem_size = std::min(stem.size(), short_name_stem_size);
    const size_t ext_size = std::min(ext.size(), short_name_ext_size);

    for (size_t i = 0; i < stem_size; ++i) {
        name[i] = static_cast<uint8_t>(stem[i]);
    }
    for (size_t i = 0; i < ext_size; ++i) {
        name[short_name_stem_size + i] = static_cast<uint8_t>(ext[i]);
    }

    return name;
}

[[nodiscard]] std::array<uint8_t, 12> make_zero_timestamp() {
    std::array<uint8_t, 12> timestamp{};
    for (uint8_t& byte : timestamp) {
        byte = static_cast<uint8_t>('0');
    }
    return timestamp;
}

void write_padded_string(
    std::vector<uint8_t>& bytes,
    size_t offset,
    size_t capacity,
    std::string_view value
) {
    if (offset >= bytes.size() || capacity == 0) {
        return;
    }

    const size_t max_size = std::min(capacity, bytes.size() - offset);
    const size_t copy_size = std::min(max_size, value.size());
    std::memcpy(bytes.data() + offset, value.data(), copy_size);
    if (copy_size < max_size) {
        bytes[offset + copy_size] = 0;
    }
}

void append_bytes(std::vector<uint8_t>& bytes, std::span<const uint8_t> more) {
    bytes.insert(bytes.end(), more.begin(), more.end());
}

[[nodiscard]] std::pair<uint32_t, uint32_t> frame_rate_ratio(uint8_t frame_rate_code) {
    switch (frame_rate_code) {
        case 1: return {24000, 1001};
        case 2: return {24, 1};
        case 3: return {25, 1};
        case 4: return {30000, 1001};
        case 5: return {30, 1};
        case 6: return {50, 1};
        case 7: return {60000, 1001};
        case 8: return {60, 1};
        default: return {30000, 1001};
    }
}

[[nodiscard]] std::optional<SfdVideoSequenceHeader> parse_video_sequence_header(std::span<const uint8_t> bytes) {
    for (size_t offset = 0; offset + 12 <= bytes.size(); ++offset) {
        if (bytes[offset + 0] != 0x00 || bytes[offset + 1] != 0x00 || bytes[offset + 2] != 0x01 || bytes[offset + 3] != 0xB3) {
            continue;
        }

        return SfdVideoSequenceHeader{
            .width = static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset + 4]) << 4u) | (bytes[offset + 5] >> 4u)),
            .height = static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset + 5] & 0x0Fu) << 8u) | bytes[offset + 6]),
            .aspect_ratio_code = static_cast<uint8_t>((bytes[offset + 7] >> 4u) & 0x0Fu),
            .frame_rate_code = static_cast<uint8_t>(bytes[offset + 7] & 0x0Fu),
            .bit_rate_value = static_cast<uint32_t>((static_cast<uint32_t>(bytes[offset + 8]) << 10u) |
                (static_cast<uint32_t>(bytes[offset + 9]) << 2u) |
                (static_cast<uint32_t>(bytes[offset + 10]) >> 6u)),
        };
    }
    return std::nullopt;
}

[[nodiscard]] SfdVideoType detect_video_type(std::span<const uint8_t> bytes) {
    bool saw_sequence = false;
    for (size_t offset = 0; offset + 5 <= bytes.size(); ++offset) {
        if (bytes[offset + 0] != 0x00 || bytes[offset + 1] != 0x00 || bytes[offset + 2] != 0x01) {
            continue;
        }
        if (bytes[offset + 3] == 0xB3) {
            saw_sequence = true;
        } else if (bytes[offset + 3] == 0xB5 && saw_sequence) {
            const uint8_t extension_id = static_cast<uint8_t>((bytes[offset + 4] >> 4u) & 0x0Fu);
            if (extension_id == 0x01) {
                return SfdVideoType::mpeg2;
            }
        }
    }
    return saw_sequence ? SfdVideoType::mpeg1 : SfdVideoType::unknown;
}

[[nodiscard]] std::vector<TimedUnit> split_video_units(
    std::span<const uint8_t> bytes,
    double frame_duration_seconds
) {
    std::vector<TimedUnit> units;
    size_t current_frame_start = std::numeric_limits<size_t>::max();
    size_t next_frame_start = std::numeric_limits<size_t>::max();
    size_t frame_index = 0;

    auto is_prefix_header = [](uint8_t start_code) noexcept {
        return start_code == 0xB2 || start_code == 0xB3 || start_code == 0xB5 || start_code == 0xB8;
    };

    for (size_t offset = 0; offset + 4 <= bytes.size(); ++offset) {
        if (bytes[offset + 0] != 0x00 || bytes[offset + 1] != 0x00 || bytes[offset + 2] != 0x01) {
            continue;
        }

        const uint8_t start_code = bytes[offset + 3];
        if (start_code == 0x00) {
            if (current_frame_start != std::numeric_limits<size_t>::max()) {
                const size_t frame_end = next_frame_start != std::numeric_limits<size_t>::max() ? next_frame_start : offset;
                if (frame_end > current_frame_start) {
                    units.push_back(TimedUnit{
                        .offset = current_frame_start,
                        .size = frame_end - current_frame_start,
                        .time_seconds = frame_index * frame_duration_seconds,
                    });
                    ++frame_index;
                }
            }

            current_frame_start = next_frame_start != std::numeric_limits<size_t>::max() ? next_frame_start : offset;
            next_frame_start = std::numeric_limits<size_t>::max();
        } else if (is_prefix_header(start_code) && next_frame_start == std::numeric_limits<size_t>::max()) {
            next_frame_start = offset;
        }
    }

    if (current_frame_start != std::numeric_limits<size_t>::max() && current_frame_start < bytes.size()) {
        units.push_back(TimedUnit{
            .offset = current_frame_start,
            .size = bytes.size() - current_frame_start,
            .time_seconds = frame_index * frame_duration_seconds,
        });
    }

    if (units.empty() && !bytes.empty()) {
        units.push_back(TimedUnit{
            .offset = 0,
            .size = bytes.size(),
            .time_seconds = 0.0,
        });
    }

    return units;
}

struct VideoSourceDescriptor {
    uint8_t source_type = 1;
    std::string default_source_name;
};

[[nodiscard]] VideoSourceDescriptor resolve_video_source_descriptor(
    const std::filesystem::path& path,
    SfdVideoType video_type
) {
    const std::string extension = lowercase_ascii(path.extension().string());
    if (extension == ".sfv") {
        return {
            .source_type = 0,
            .default_source_name = replace_extension(path, ".sfv"),
        };
    }
    if (extension == ".m1v") {
        return {
            .source_type = 1,
            .default_source_name = replace_extension(path, ".m1v"),
        };
    }
    if (extension == ".mpv") {
        return {
            .source_type = 2,
            .default_source_name = replace_extension(path, ".mpv"),
        };
    }
    if (extension == ".m2v") {
        return {
            .source_type = 3,
            .default_source_name = replace_extension(path, ".m2v"),
        };
    }

    switch (video_type) {
        case SfdVideoType::mpeg2: {
            return {
                .source_type = 3,
                .default_source_name = replace_extension(path, ".m2v"),
            };
        }
        case SfdVideoType::mpeg1: {
            return {
                .source_type = 1,
                .default_source_name = replace_extension(path, ".m1v"),
            };
        }
        case SfdVideoType::unknown:
        default: {
            return {
                .source_type = 1,
                .default_source_name = path.filename().string(),
            };
        }
    }
}

[[nodiscard]] std::expected<VideoSource, std::string> load_video_source(const SfdBuildInput& input) {
    auto bytes_result = io::read_file_bytes(input.video_path, "SFD load failed");
    if (!bytes_result) {
        return std::unexpected(bytes_result.error());
    }

    auto sequence_header = parse_video_sequence_header(*bytes_result);
    if (!sequence_header) {
        return std::unexpected("SFD builder requires MPEG video input with a sequence header");
    }

    VideoSource video;
    video.bytes = std::move(*bytes_result);
    video.sequence_header = *sequence_header;
    video.video_type = detect_video_type(video.bytes);
    const auto [fps_n, fps_d] = frame_rate_ratio(video.sequence_header.frame_rate_code);
    video.fps_n = fps_n;
    video.fps_d = fps_d;
    const auto source_descriptor = resolve_video_source_descriptor(input.video_path, video.video_type);
    video.source_type = source_descriptor.source_type;
    auto source_name = resolve_source_name(
        input.video_source_name,
        input.video_stream_name,
        source_descriptor.default_source_name,
        "video"
    );
    if (!source_name) {
        return std::unexpected(source_name.error());
    }
    video.source_name = std::string(*source_name);

    const double frame_duration_seconds = static_cast<double>(video.fps_d) / static_cast<double>(video.fps_n);
    video.units = split_video_units(video.bytes, frame_duration_seconds);
    video.duration_seconds = video.units.empty()
        ? 0.0
        : video.units.back().time_seconds + frame_duration_seconds;

    return video;
}

[[nodiscard]] std::string default_audio_source_name(const std::filesystem::path& path, SfdAudioType audio_type) {
    switch (audio_type) {
        case SfdAudioType::adx:
            return replace_extension(path, ".sfa");
        case SfdAudioType::aix:
            return replace_extension(path, ".aix");
        case SfdAudioType::ac3:
            return replace_extension(path, ".ac3");
        case SfdAudioType::unknown:
        default:
            return path.filename().string();
    }
}

[[nodiscard]] std::expected<AudioSource, std::string> load_audio_source(
    const SfdBuildInput& input,
    const VideoSource& video
) {
    if (!input.audio_path.has_value()) {
        return std::unexpected("SFD build failed: no audio input");
    }

    auto bytes_result = io::read_file_bytes(*input.audio_path, "SFD load failed");
    if (!bytes_result) {
        return std::unexpected(bytes_result.error());
    }

    adx::AdxDecoder decoder;
    if (auto result = decoder.load(*bytes_result); !result) {
        return std::unexpected("SFD builder currently supports ADX-like audio input only: " + result.error());
    }

    AudioSource audio;
    audio.bytes = std::move(*bytes_result);
    audio.header = decoder.header();
    audio.audio_type = SfdAudioType::adx;
    audio.samples_per_block = ((audio.header.block_size - 2u) * 8u) / audio.header.bit_depth;
    audio.data_start = static_cast<size_t>(audio.header.data_offset) + 4u;
    audio.duration_seconds = audio.header.sample_rate == 0
        ? 0.0
        : static_cast<double>(audio.header.sample_count) / static_cast<double>(audio.header.sample_rate);
    const std::string fallback_source_name = default_audio_source_name(*input.audio_path, audio.audio_type);
    auto source_name = resolve_source_name(
        input.audio_source_name,
        input.audio_stream_name,
        fallback_source_name,
        "audio"
    );
    if (!source_name) {
        return std::unexpected(source_name.error());
    }
    audio.source_name = std::string(*source_name);

    // `ref/docs/sfd_porting_notes.md` indicates the fixed-pack builder keeps
    // chunk timing in-frame and rounds up to include any partial sample span.
    const uint32_t target_samples = std::max<uint32_t>(1u,
        static_cast<uint32_t>(cricodecs::util::divide_round_up(
            static_cast<uint64_t>(audio.header.sample_rate) * static_cast<uint64_t>(video.fps_d),
            video.fps_n)));
    const uint32_t blocks_per_chunk = std::max<uint32_t>(1u,
        cricodecs::util::divide_round_up(target_samples, audio.samples_per_block));
    const size_t interleaved_block_size = static_cast<size_t>(audio.header.block_size) * audio.header.channels;
    const size_t target_chunk_bytes = static_cast<size_t>(blocks_per_chunk) * interleaved_block_size;

    size_t offset = 0;
    uint32_t emitted_samples = 0;
    while (offset < audio.bytes.size()) {
        size_t chunk_size = 0;
        if (offset == 0) {
            const size_t header_bytes = std::min(audio.bytes.size(), audio.data_start);
            const size_t initial_audio_bytes = std::min(audio.bytes.size() - header_bytes, target_chunk_bytes);
            chunk_size = header_bytes + initial_audio_bytes;
        } else {
            chunk_size = std::min(audio.bytes.size() - offset, target_chunk_bytes);
        }

        if (chunk_size == 0) {
            break;
        }

        audio.units.push_back(TimedUnit{
            .offset = offset,
            .size = chunk_size,
            .time_seconds = audio.header.sample_rate == 0
                ? 0.0
                : static_cast<double>(emitted_samples) / static_cast<double>(audio.header.sample_rate),
        });

        if (offset == 0) {
            const size_t data_bytes = chunk_size > audio.data_start ? chunk_size - audio.data_start : 0;
            const uint32_t blocks = static_cast<uint32_t>(data_bytes / interleaved_block_size);
            emitted_samples += blocks * audio.samples_per_block;
        } else {
            const uint32_t blocks = static_cast<uint32_t>(chunk_size / interleaved_block_size);
            emitted_samples += blocks * audio.samples_per_block;
        }
        offset += chunk_size;
    }

    return audio;
}

[[nodiscard]] uint32_t compute_payload_capacity(bool with_pts) {
    const uint32_t pes_optional_size = with_pts ? 5u : 0u;
    return sector_size - pack_header_size - packet_header_size - pes_optional_size;
}

void append_pts_mpeg1(std::vector<uint8_t>& bytes, uint64_t pts90k) {
    const uint64_t pts = pts90k & ((1ull << 33u) - 1u);
    bytes.push_back(static_cast<uint8_t>(0x20u | (((pts >> 30u) & 0x07u) << 1u) | 0x01u));
    bytes.push_back(static_cast<uint8_t>((pts >> 22u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((((pts >> 15u) & 0x7Fu) << 1u) | 0x01u));
    bytes.push_back(static_cast<uint8_t>((pts >> 7u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>(((pts & 0x7Fu) << 1u) | 0x01u));
}

void write_pack_header(std::vector<uint8_t>& sector, size_t offset, uint64_t scr90k, uint32_t mux_rate_units) {
    sector[offset + 0] = 0x00;
    sector[offset + 1] = 0x00;
    sector[offset + 2] = 0x01;
    sector[offset + 3] = 0xBA;

    const uint64_t scr = scr90k & ((1ull << 33u) - 1u);
    sector[offset + 4] = static_cast<uint8_t>(0x20u | (((scr >> 30u) & 0x07u) << 1u) | 0x01u);
    sector[offset + 5] = static_cast<uint8_t>((scr >> 22u) & 0xFFu);
    sector[offset + 6] = static_cast<uint8_t>((((scr >> 15u) & 0x7Fu) << 1u) | 0x01u);
    sector[offset + 7] = static_cast<uint8_t>((scr >> 7u) & 0xFFu);
    sector[offset + 8] = static_cast<uint8_t>(((scr & 0x7Fu) << 1u) | 0x01u);
    sector[offset + 9] = static_cast<uint8_t>((((mux_rate_units >> 15u) & 0x7Fu) << 1u) | 0x01u);
    sector[offset + 10] = static_cast<uint8_t>((mux_rate_units >> 7u) & 0xFFu);
    sector[offset + 11] = static_cast<uint8_t>(((mux_rate_units & 0x7Fu) << 1u) | 0x01u);
}

void write_system_header(std::vector<uint8_t>& sector, size_t offset, uint32_t mux_rate_units, uint8_t primary_stream_id) {
    sector[offset + 0] = 0x00;
    sector[offset + 1] = 0x00;
    sector[offset + 2] = 0x01;
    sector[offset + 3] = static_cast<uint8_t>(packet_system_header);
    sector[offset + 4] = 0x00;
    sector[offset + 5] = 0x09;

    sector[offset + 6] = static_cast<uint8_t>((((mux_rate_units >> 15u) & 0x7Fu) << 1u) | 0x01u);
    sector[offset + 7] = static_cast<uint8_t>((mux_rate_units >> 7u) & 0xFFu);
    sector[offset + 8] = static_cast<uint8_t>(((mux_rate_units & 0x7Fu) << 1u) | 0x01u);
    sector[offset + 9] = 0x06;
    sector[offset + 10] = 0x20;
    sector[offset + 11] = 0xFF;
    sector[offset + 12] = primary_stream_id;
    sector[offset + 13] = primary_stream_id;
    sector[offset + 14] = 0x04;
}

[[nodiscard]] uint32_t calculate_mux_rate_units(uint64_t total_size, double duration_seconds) {
    if (duration_seconds <= 0.0) {
        return 1;
    }

    const double bytes_per_second = static_cast<double>(total_size) / duration_seconds;
    return std::max<uint32_t>(1u, static_cast<uint32_t>(std::ceil(bytes_per_second / 50.0)));
}

[[nodiscard]] std::vector<uint8_t> build_initial_sector0(uint8_t primary_stream_id, uint32_t mux_rate_units) {
    std::vector<uint8_t> sector(sector_size, 0xFF);
    write_pack_header(sector, 0, 0, mux_rate_units);
    write_system_header(sector, pack_header_size, mux_rate_units, primary_stream_id);

    // Build the final sector-padding packet for the first sector exactly as
    // SofDec does: close the sector with stream-id 0xBE when bytes remain.
    // This mirrors the sector-constrained packet layout in
    // `ref/docs/sfd_porting_notes.md`.
    const size_t padding_offset = pack_header_size + 15u;
    sector[padding_offset + 0] = 0x00;
    sector[padding_offset + 1] = 0x00;
    sector[padding_offset + 2] = 0x01;
    sector[padding_offset + 3] = packet_padding_stream;
    const uint16_t padding_length = static_cast<uint16_t>(sector_size - padding_offset - packet_header_size);
    sector[padding_offset + 4] = static_cast<uint8_t>((padding_length >> 8u) & 0xFFu);
    sector[padding_offset + 5] = static_cast<uint8_t>(padding_length & 0xFFu);
    return sector;
}

void write_record_common(
    std::vector<uint8_t>& payload,
    size_t offset,
    std::string_view source_name,
    uint8_t stream_id
) {
    const auto short_name = make_short_name(source_name);
    const auto timestamp = make_zero_timestamp();
    std::memcpy(payload.data() + offset + 0, short_name.data(), short_name.size());
    std::memcpy(payload.data() + offset + 12, timestamp.data(), timestamp.size());
    payload[offset + 24] = stream_id;
}

[[nodiscard]] std::vector<uint8_t> build_sofdec_header_payload(
    const MuxProfileDescriptor& profile,
    const SfdBuildInput& input,
    std::string_view header_builder_version,
    const VideoSource& video,
    const std::optional<AudioSource>& audio
) {
    std::vector<uint8_t> payload(sofdec_header_payload_size, 0);
    const auto& layout = profile.layout;

    if (profile.variant == SfdHeaderVariant::sofdec_stream2) {
        payload[0] = 0x08;
    }

    std::memcpy(payload.data() + layout.label_offset, profile.label.data(), profile.label.size());
    for (size_t i = 0; i < profile.version_tag_size; ++i) {
        payload[layout.version_offset + i] = profile.version_tag[i];
    }

    write_padded_string(
        payload,
        layout.builder_version_offset,
        layout.builder_version_size,
        header_builder_version
    );

    const auto short_output_name = make_short_name(
        input.output_name.empty() ? std::string("OUTPUT.SFD") : input.output_name);
    if (layout.short_output_name_offset != 0) {
        const auto output_timestamp = make_zero_timestamp();
        std::memcpy(payload.data() + layout.short_output_name_offset, short_output_name.data(), short_output_name.size());
        std::memcpy(payload.data() + layout.output_timestamp_offset, output_timestamp.data(), output_timestamp.size());
    }

    if (layout.pack_size_offset != 0) {
        write_le<uint32_t>(payload.data() + layout.pack_size_offset + 0, sector_size);
        payload[layout.pack_size_offset + 4] = 0;
        write_le<uint16_t>(payload.data() + layout.min_header_packets_offset, 2);
        write_le<uint32_t>(payload.data() + layout.reserved_header_size_offset, sector_size);
    }

    const uint8_t audio_count = audio.has_value() ? 1u : 0u;
    payload[layout.stream_counts_offset + 0] = static_cast<uint8_t>(1u + audio_count);
    payload[layout.stream_counts_offset + 1] = audio_count;
    payload[layout.stream_counts_offset + 2] = 1;
    payload[layout.stream_counts_offset + 3] = 0;

    std::string output_name_string = input.output_name.empty()
        ? std::string("output.sfd")
        : input.output_name;
    write_padded_string(payload, layout.output_name_offset, 64, output_name_string);

    if (!layout.has_standard_element_table) {
        return payload;
    }

    size_t record_offset = layout.element_table_offset;
    write_record_common(payload, record_offset, video.source_name, packet_video_stream_0);
    payload[record_offset + 25] = video.source_type;
    const auto [fps_n, fps_d] = frame_rate_ratio(video.sequence_header.frame_rate_code);
    write_le<uint16_t>(payload.data() + record_offset + 26, static_cast<uint16_t>(std::lround(
        (static_cast<double>(fps_n) / static_cast<double>(fps_d)) * 900.0)));
    payload[record_offset + 28] = static_cast<uint8_t>(video.sequence_header.width >> 4u);
    payload[record_offset + 29] = static_cast<uint8_t>(((video.sequence_header.width & 0x0Fu) << 4u) |
        ((video.sequence_header.height >> 8u) & 0x0Fu));
    payload[record_offset + 30] = static_cast<uint8_t>(video.sequence_header.height & 0xFFu);
    payload[record_offset + 31] = video.sequence_header.frame_rate_code;

    if (audio.has_value()) {
        record_offset += layout.element_record_size;
        write_record_common(payload, record_offset, audio->source_name, packet_audio_stream_0);
        payload[record_offset + 25] = 0;
        payload[record_offset + 27] = audio->header.channels;
        write_le<uint32_t>(payload.data() + record_offset + 28, audio->header.sample_rate);
    }

    return payload;
}

[[nodiscard]] std::vector<uint8_t> build_header_sector(std::span<const uint8_t> sofdec_header_payload, uint32_t mux_rate_units) {
    std::vector<uint8_t> sector(sector_size, 0);
    write_pack_header(sector, 0, 0, mux_rate_units);
    sector[pack_header_size + 0] = 0x00;
    sector[pack_header_size + 1] = 0x00;
    sector[pack_header_size + 2] = 0x01;
    sector[pack_header_size + 3] = packet_private_stream_2;
    sector[pack_header_size + 4] = static_cast<uint8_t>((sofdec_header_payload.size() >> 8u) & 0xFFu);
    sector[pack_header_size + 5] = static_cast<uint8_t>(sofdec_header_payload.size() & 0xFFu);
    std::memcpy(sector.data() + pack_header_size + packet_header_size,
        sofdec_header_payload.data(),
        sofdec_header_payload.size());
    return sector;
}

[[nodiscard]] std::vector<uint8_t> build_data_sector(
    StreamCursor& cursor,
    uint64_t scr90k,
    uint32_t mux_rate_units
) {
    const uint32_t payload_capacity = compute_payload_capacity(true);
    size_t payload_size = std::min<size_t>(cursor.remaining_in_unit(), payload_capacity);
    size_t remaining = sector_size - (pack_header_size + packet_header_size + 5u + payload_size);
    if (remaining > 0 && remaining < packet_header_size) {
        const size_t adjustment = packet_header_size - remaining;
        if (adjustment <= payload_size) {
            payload_size -= adjustment;
            remaining += adjustment;
        }
    }

    std::vector<uint8_t> sector;
    sector.reserve(sector_size);
    sector.resize(pack_header_size, 0);
    write_pack_header(sector, 0, scr90k, mux_rate_units);

    sector.push_back(0x00);
    sector.push_back(0x00);
    sector.push_back(0x01);
    sector.push_back(cursor.stream_id);
    append_be<uint16_t>(sector, static_cast<uint16_t>(payload_size + 5u));
    append_pts_mpeg1(sector, scr90k);

    const size_t source_offset = cursor.absolute_offset();
    sector.insert(sector.end(),
        cursor.bytes->begin() + static_cast<std::ptrdiff_t>(source_offset),
        cursor.bytes->begin() + static_cast<std::ptrdiff_t>(source_offset + payload_size));

    remaining = sector_size - sector.size();
    if (remaining >= packet_header_size) {
        // Packet padding keeps each sector closed to 0x000001BE when the next
        // payload would otherwise leave unused bytes in the transport sector.
        // This follows the same sector-boundary closure behavior in
        // `ref/docs/sfd_porting_notes.md`.
        sector.push_back(0x00);
        sector.push_back(0x00);
        sector.push_back(0x01);
        sector.push_back(packet_padding_stream);
        const uint16_t padding_length = static_cast<uint16_t>(remaining - packet_header_size);
        append_be<uint16_t>(sector, padding_length);
        sector.insert(sector.end(), padding_length, 0xFF);
    }
    sector.resize(sector_size, 0xFF);

    cursor.advance(payload_size);
    return sector;
}

[[nodiscard]] std::vector<uint8_t> build_end_sector() {
    std::vector<uint8_t> sector(sector_size, 0);
    sector[0] = 0x00;
    sector[1] = 0x00;
    sector[2] = 0x01;
    sector[3] = packet_program_end;
    return sector;
}

void patch_header_bitrate_and_counts(
    const MuxProfileDescriptor& profile,
    std::vector<uint8_t>& output,
    uint64_t total_size,
    double duration_seconds,
    bool has_audio
) {
    const auto& layout = profile.layout;
    const uint64_t bitrate_bytes_per_second = duration_seconds <= 0.0
        ? 0
        : static_cast<uint64_t>(std::llround(static_cast<double>(total_size) / duration_seconds));

    const size_t payload_offset = sector_size + pack_header_size + packet_header_size;
    output[payload_offset + layout.stream_counts_offset + 0] = has_audio ? 2u : 1u;
    output[payload_offset + layout.stream_counts_offset + 1] = has_audio ? 1u : 0u;
    output[payload_offset + layout.stream_counts_offset + 2] = 1u;
    output[payload_offset + layout.stream_counts_offset + 3] = 0u;
    if (layout.bitrate_offset != 0) {
        write_le<uint64_t>(output.data() + payload_offset + layout.bitrate_offset, bitrate_bytes_per_second);
    }
}

void patch_sector_headers(
    std::vector<uint8_t>& output,
    std::span<const SectorPatch> patches,
    uint32_t mux_rate_units,
    uint8_t primary_stream_id
) {
    for (const auto& patch : patches) {
        write_pack_header(output, patch.offset, patch.scr, mux_rate_units);
        if (patch.has_system_header) {
            write_system_header(output, patch.offset + pack_header_size, mux_rate_units, primary_stream_id);
        }
    }
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> SfdBuilder::build(const SfdBuildInput& input) {
    if (input.video_path.empty()) {
        return std::unexpected("SFD builder requires a video input path");
    }
    auto build_profile = resolve_build_profile(input);
    if (!build_profile) {
        return std::unexpected(build_profile.error());
    }
    const auto& profile = resolve_build_profile_descriptor(*build_profile);
    auto header_builder_version = resolve_header_builder_version(input, profile);
    if (!header_builder_version) {
        return std::unexpected(header_builder_version.error());
    }

    auto video_result = load_video_source(input);
    if (!video_result) {
        return std::unexpected(video_result.error());
    }
    auto video = std::move(*video_result);

    std::optional<AudioSource> audio;
    if (input.audio_path.has_value()) {
        auto audio_result = load_audio_source(input, video);
        if (!audio_result) {
            if (audio_result.error() != "SFD build failed: no audio input") {
                return std::unexpected(audio_result.error());
            }
        } else {
            audio = std::move(*audio_result);
        }
    }

    std::vector<uint8_t> sofdec_header_payload =
        build_sofdec_header_payload(profile, input, *header_builder_version, video, audio);
    std::vector<uint8_t> output;
    output.reserve(sector_size * 4u + video.bytes.size() + (audio ? audio->bytes.size() : 0u));

    std::vector<SectorPatch> sector_patches;
    sector_patches.push_back(SectorPatch{
        .offset = output.size(),
        .scr = 0,
        .has_system_header = true,
    });
    const uint8_t primary_stream_id = audio.has_value() ? packet_audio_stream_0 : packet_video_stream_0;
    append_bytes(output, build_initial_sector0(primary_stream_id, 1));

    sector_patches.push_back(SectorPatch{
        .offset = output.size(),
        .scr = 0,
        .has_system_header = false,
    });
    append_bytes(output, build_header_sector(sofdec_header_payload, 1));

    StreamCursor video_cursor{
        .bytes = &video.bytes,
        .units = &video.units,
        .stream_id = packet_video_stream_0,
        .is_audio = false,
    };
    std::optional<StreamCursor> audio_cursor;
    if (audio.has_value()) {
        audio_cursor = StreamCursor{
            .bytes = &audio->bytes,
            .units = &audio->units,
            .stream_id = packet_audio_stream_0,
            .is_audio = true,
        };
    }

    while (!video_cursor.done() || (audio_cursor.has_value() && !audio_cursor->done())) {
        StreamCursor* chosen = nullptr;
        if (!video_cursor.done()) {
            chosen = &video_cursor;
        }
        if (audio_cursor.has_value() && !audio_cursor->done()) {
            if (chosen == nullptr || audio_cursor->next_time() < chosen->next_time()) {
                chosen = &*audio_cursor;
            }
        }

        if (chosen == nullptr) {
            break;
        }

        const uint64_t scr90k = static_cast<uint64_t>(std::llround(chosen->next_time() * 90000.0));
        sector_patches.push_back(SectorPatch{
            .offset = output.size(),
            .scr = scr90k,
            .has_system_header = false,
        });
        append_bytes(output, build_data_sector(*chosen, scr90k, 1));
    }

    append_bytes(output, build_end_sector());

    const double duration_seconds = std::max(video.duration_seconds, audio.has_value() ? audio->duration_seconds : 0.0);
    patch_header_bitrate_and_counts(profile, output, output.size(), duration_seconds, audio.has_value());
    patch_sector_headers(output, sector_patches, calculate_mux_rate_units(output.size(), duration_seconds), primary_stream_id);

    return output;
}

std::expected<void, std::string> SfdBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const SfdBuildInput& input
) {
    auto bytes = build(input);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("SFD build failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("SFD build failed: could not open output: " + output_path.string());
    }
    if (auto result = writer.write(*bytes); !result) {
        (void)writer.close();
        return std::unexpected("SFD build failed: could not write output: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("SFD build failed: could not finalize output: " + output_path.string());
    }

    return {};
}

} // namespace cricodecs::sfd
