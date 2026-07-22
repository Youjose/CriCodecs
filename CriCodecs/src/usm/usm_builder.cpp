/**
 * @file usm_builder.cpp
 * @brief USM builder
 *
 * The mux layout started from PyCriCodecsEx behavior and has since been
 * checked against Medianoche/Sofdec 2 metadata names such as
 * CRIUSF_DIR_STREAM, VIDEO_HDRINFO, and VIDEO_SEEKINFO. The current builder
 * accepts VP9-in-IVF, MPEG/Sofdec elementary video, H.264 Annex B, and
 * optional ADX or HCA audio, with byte-exact stream reassembly as the primary
 * contract rather than byte-identical USM authoring.
 */

#include "usm_container.hpp"

#include "../adx/adx_codec.hpp"
#include "../hca/hca_codec.hpp"
#include "../utilities/string.hpp"
#include "../video/h264.hpp"
#include "../video/mpeg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <system_error>
#include <tuple>

#include "../utilities/numeric.hpp"

namespace cricodecs::usm {

namespace {

using cricodecs::util::align_up;
using cricodecs::util::lowercase_ascii;

constexpr io::FourCC IvfMagic{"DKIF"};
constexpr io::FourCC Vp9Magic{"VP90"};

constexpr uint32_t vp9_codec_id = 9;
constexpr uint32_t vp9_fmtver = 16777984;
constexpr uint8_t vp9_dcprec = 0;
constexpr uint32_t mpeg1_codec_id = 1;
constexpr uint32_t mpeg_fmtver = 0;
constexpr uint8_t mpeg_dcprec = 11;
constexpr uint32_t h264_codec_id = 5;
constexpr uint32_t base_frame_rate = 2997;
constexpr double audio_chunk_interval = 99.9;
constexpr uint32_t audio_ixsize = 27860;
constexpr std::array<uint8_t, 0x20> contents_end_marker = {
    '#','C','O','N','T','E','N','T','S',' ','E','N','D',' ',' ',' ',
    '=','=','=','=','=','=','=','=','=','=','=','=','=','=',0x00
};
constexpr std::array<uint8_t, 0x20> header_end_marker = {
    '#','H','E','A','D','E','R',' ','E','N','D',' ',' ',' ',' ',' ',
    '=','=','=','=','=','=','=','=','=','=','=','=','=','=',0x00
};
constexpr std::array<uint8_t, 0x20> metadata_end_marker = {
    '#','M','E','T','A','D','A','T','A',' ','E','N','D',' ',' ',' ',
    '=','=','=','=','=','=','=','=','=','=','=','=','=','=',0x00
};

struct BuiltChunk {
    UsmChunk chunk;
    uint32_t priority = 0;
    bool is_keyframe = false;
    uint32_t frame_index = 0;
};

struct VideoSeekEntry {
    uint64_t byte_offset = 0;
    uint32_t frame_index = 0;
};

struct VideoBuildInfo {
    std::string filename;
    uint32_t filesize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frame_count = 0;
    uint32_t framerate_n = 30000;
    uint32_t framerate_d = 1000;
    uint32_t fmtver = vp9_fmtver;
    uint32_t codec_id = vp9_codec_id;
    uint8_t dcprec = vp9_dcprec;
    uint32_t minbuf = 0;
    uint32_t avbps = 0;
    std::vector<BuiltChunk> chunks;
};

struct AudioBuildInfo {
    std::string filename;
    uint32_t filesize = 0;
    uint32_t sample_rate = 0;
    uint32_t total_samples = 0;
    uint8_t channels = 0;
    uint8_t channel_no = 0;
    UsmAudioCodec codec = UsmAudioCodec::Adx;
    uint32_t avbps = 0;
    std::vector<UsmChunk> metadata_chunks;
    std::vector<BuiltChunk> chunks;
};

struct SubtitleBuildInfo {
    std::string filename;
    uint32_t filesize = 0;
    uint32_t time_unit = 1000;
    uint32_t total_time = 0;
    uint32_t channel_count = 0;
    uint32_t content_size = 0;
    uint32_t ixsize = 0;
    uint32_t avbps = 0;
    uint8_t channel_no = 0;
    std::vector<BuiltChunk> chunks;
};

template <class Track>
std::expected<std::vector<uint8_t>, std::string> resolve_track_channels(
    std::span<const Track> tracks,
    std::string_view track_kind
) {
    constexpr size_t channel_count = static_cast<size_t>(std::numeric_limits<uint8_t>::max()) + 1u;
    if (tracks.size() > channel_count) {
        return std::unexpected(
            "USM build failed: " + std::string(track_kind) + " track count exceeds 256 channels"
        );
    }

    std::array<bool, channel_count> used{};
    std::vector<uint8_t> channels(tracks.size());
    for (size_t index = 0; index < tracks.size(); ++index) {
        if (!tracks[index].channel_no.has_value()) {
            continue;
        }
        const auto channel = *tracks[index].channel_no;
        if (used[channel]) {
            return std::unexpected(
                "USM build failed: duplicate " + std::string(track_kind) +
                " channel " + std::to_string(channel)
            );
        }
        used[channel] = true;
        channels[index] = channel;
    }

    size_t next_channel = 0;
    for (size_t index = 0; index < tracks.size(); ++index) {
        if (tracks[index].channel_no.has_value()) {
            continue;
        }
        while (next_channel < used.size() && used[next_channel]) {
            ++next_channel;
        }
        if (next_channel == used.size()) {
            return std::unexpected(
                "USM build failed: no " + std::string(track_kind) + " channel remains available"
            );
        }
        channels[index] = static_cast<uint8_t>(next_channel);
        used[next_channel] = true;
    }
    return channels;
}

std::expected<UsmAudioCodec, std::string> probe_audio_codec(const std::filesystem::path& path) {
    io::reader reader;
    if (auto opened = reader.open(path); !opened) {
        return std::unexpected(
            "USM build failed: could not open audio input `" + path.string() + "`: " + opened.error()
        );
    }
    const auto bytes = reader.data();
    if (bytes.size() >= sizeof(uint32_t) &&
        (io::read_be<uint32_t>(bytes.data()) & hca::HCA_MASK) == hca::HCA_CHUNK_ID_HCA) {
        auto hca_source = hca::Hca::load(path);
        if (!hca_source) {
            return std::unexpected("USM build failed: could not inspect HCA audio: " + hca_source.error());
        }
        return UsmAudioCodec::Hca;
    }
    if (bytes.size() >= sizeof(uint16_t) && io::read_be<uint16_t>(bytes.data()) == 0x8000u) {
        adx::AdxDecoder decoder;
        if (auto loaded = decoder.load(path.string()); !loaded) {
            return std::unexpected("USM build failed: could not inspect ADX audio: " + loaded.error());
        }
        if (decoder.is_ahx()) {
            return std::unexpected("USM build failed: AHX audio requires an @AHX mux path, which is not implemented");
        }
        return UsmAudioCodec::Adx;
    }
    return std::unexpected(
        "USM build failed: audio input is neither an ADX nor HCA stream: " + path.string()
    );
}

std::expected<UsmBuildPlan, std::string> plan_build_impl(const UsmBuildInput& input) {
    if (input.video_path.empty()) {
        return std::unexpected("USM build requires a video input path");
    }
    std::error_code filesystem_error;
    if (!std::filesystem::exists(input.video_path, filesystem_error) || filesystem_error) {
        return std::unexpected("USM build failed: video input does not exist: " + input.video_path.string());
    }

    auto audio_channels = resolve_track_channels(
        std::span<const UsmBuildInput::AudioTrack>(input.audio_tracks),
        "audio"
    );
    if (!audio_channels) {
        return std::unexpected(audio_channels.error());
    }
    auto subtitle_channels = resolve_track_channels(
        std::span<const UsmBuildInput::SubtitleTrack>(input.subtitle_tracks),
        "subtitle"
    );
    if (!subtitle_channels) {
        return std::unexpected(subtitle_channels.error());
    }

    UsmBuildPlan plan;
    plan.audio_tracks.reserve(input.audio_tracks.size());
    for (size_t index = 0; index < input.audio_tracks.size(); ++index) {
        const auto& track = input.audio_tracks[index];
        const bool encrypt = track.encrypt.value_or(input.encrypt_audio.value_or(input.key != 0));
        if (encrypt && input.key == 0) {
            return std::unexpected(
                "USM build failed: audio encryption requires a nonzero key"
            );
        }
        auto codec = probe_audio_codec(track.path);
        if (!codec) {
            return std::unexpected(codec.error());
        }
        plan.audio_tracks.push_back(UsmBuildPlan::AudioTrack{
            .channel_no = (*audio_channels)[index],
            .codec = *codec,
            .encrypt = encrypt,
        });
    }

    plan.subtitle_tracks.reserve(input.subtitle_tracks.size());
    for (size_t index = 0; index < input.subtitle_tracks.size(); ++index) {
        const auto& track = input.subtitle_tracks[index];
        filesystem_error.clear();
        if (track.path.empty() || !std::filesystem::exists(track.path, filesystem_error) || filesystem_error) {
            return std::unexpected(
                "USM build failed: subtitle input does not exist: " + track.path.string()
            );
        }
        plan.subtitle_tracks.push_back(UsmBuildPlan::SubtitleTrack{
            .channel_no = (*subtitle_channels)[index],
        });
    }
    return plan;
}

UsmChunk make_chunk(
    UsmChunkType magic,
    UsmPayloadType type,
    uint8_t channel_no,
    uint32_t frame_time,
    uint32_t frame_rate,
    std::span<const uint8_t> payload
) {
    // USM packet payloads are emitted on 0x20 boundaries so the container
    // parser can jump between chunk bodies deterministically.
    const auto padding = align_up(static_cast<uint32_t>(payload.size()), 0x20) -
        static_cast<uint32_t>(payload.size());

    UsmChunkHeader header;
    header.magic = static_cast<uint32_t>(magic);
    header.chunk_size = static_cast<uint32_t>(payload.size()) + padding + UsmChunkHeader::encoded_header_size;
    header.offset = 0x18;
    header.padding = static_cast<uint16_t>(padding);
    header.channel_no = channel_no;
    header.type = static_cast<uint8_t>(type);
    header.frame_time = frame_time;
    header.frame_rate = frame_rate;

    return UsmChunk{
        .header = header,
        .payload = std::vector<uint8_t>(payload.begin(), payload.end()),
        .padding = {},
    };
}

UsmChunk make_end_chunk(UsmChunkType magic, uint8_t channel_no, std::span<const uint8_t> marker) {
    return make_chunk(magic, UsmPayloadType::SectionEnd, channel_no, 0, 30, marker);
}

void transform_stream_chunk_payload_with_padding(UsmChunk& chunk, const UsmCrypto* crypto, bool audio) {
    if (crypto == nullptr || !crypto->has_key()) {
        return;
    }

    const size_t payload_size = chunk.payload.size();
    std::vector<uint8_t> padded_payload(chunk.payload.begin(), chunk.payload.end());
    padded_payload.resize(padded_payload.size() + chunk.header.padding, 0);
    if (audio) {
        crypto->encrypt_audio(padded_payload);
    } else {
        crypto->encrypt_video(padded_payload);
    }

    chunk.payload = UsmPayload(std::vector<uint8_t>(
        padded_payload.begin(),
        padded_payload.begin() + static_cast<std::ptrdiff_t>(payload_size)));
    if (chunk.header.padding != 0) {
        chunk.padding = UsmPayload(std::vector<uint8_t>(
            padded_payload.begin() + static_cast<std::ptrdiff_t>(payload_size),
            padded_payload.end()));
    }
}

std::expected<std::string, std::string> encode_path_filename(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    auto encoded = text::encode_cri_string(path.filename().string(), encoding);
    if (!encoded) {
        return std::unexpected("USM build failed: could not encode CRID filename: " + encoded.error());
    }
    return std::string(encoded->begin(), encoded->end());
}

UsmSubtitleFormat resolve_subtitle_format(const std::filesystem::path& path, UsmSubtitleFormat format) {
    if (format != UsmSubtitleFormat::Auto) {
        return format;
    }
    const auto extension = lowercase_ascii(path.extension().string());
    if (extension == ".srt") {
        return UsmSubtitleFormat::Srt;
    }
    if (extension == ".ass" || extension == ".ssa") {
        return UsmSubtitleFormat::Ass;
    }
    if (extension == ".sbt") {
        return UsmSubtitleFormat::Sbt;
    }
    return UsmSubtitleFormat::SourceText;
}

std::expected<std::string, std::string> read_text_file(const std::filesystem::path& path) {
    auto bytes = io::read_file_bytes(path, "USM build subtitle input read failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return std::string(bytes->begin(), bytes->end());
}

std::expected<std::vector<uint8_t>, std::string> build_subtitle_payload(
    const UsmBuildInput::SubtitleTrack& track
) {
    const auto format = resolve_subtitle_format(track.path, track.format);
    if (format == UsmSubtitleFormat::Sbt) {
        auto bytes = io::read_file_bytes(track.path, "USM build subtitle input read failed");
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto parsed = parse_sbt_subtitles(*bytes);
        if (!parsed) {
            return std::unexpected("USM build failed: invalid SBT subtitle input: " + parsed.error());
        }
        return bytes;
    }

    auto text = read_text_file(track.path);
    if (!text) {
        return std::unexpected(text.error());
    }
    switch (format) {
    case UsmSubtitleFormat::SourceText:
        return subtitle_source_text_to_sbt(*text, track.language_id);
    case UsmSubtitleFormat::Srt:
        return srt_to_sbt(*text, track.language_id);
    case UsmSubtitleFormat::Ass:
        return ass_to_sbt(*text, track.language_id);
    case UsmSubtitleFormat::Auto:
    case UsmSubtitleFormat::Sbt:
        break;
    }
    return std::unexpected("USM build failed: unsupported subtitle input format");
}

std::expected<SubtitleBuildInfo, std::string> build_subtitle_chunks(
    const UsmBuildInput::SubtitleTrack& track,
    uint8_t channel_no,
    const text::EncodingOptions& encoding
) {
    auto payload = build_subtitle_payload(track);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    auto cues = parse_sbt_subtitles(*payload);
    if (!cues) {
        return std::unexpected(cues.error());
    }
    if (cues->empty()) {
        return std::unexpected("USM build failed: subtitle input has no cues");
    }

    SubtitleBuildInfo info;
    auto filename = text::encode_cri_string(":" + track.path.filename().string(), encoding);
    if (!filename) {
        return std::unexpected("USM build failed: could not encode subtitle filename: " + filename.error());
    }
    info.filename = std::string(filename->begin(), filename->end());
    info.filesize = static_cast<uint32_t>(payload->size());
    info.content_size = info.filesize;
    info.channel_no = channel_no;

    std::vector<uint32_t> language_ids;
    language_ids.reserve(cues->size());
    uint32_t max_record_size = 0;
    uint32_t max_end_time = 0;
    info.time_unit = cues->front().time_unit;
    for (const auto& cue : *cues) {
        if (cue.time_unit != info.time_unit) {
            return std::unexpected("USM build failed: mixed subtitle time units are unsupported");
        }
        max_end_time = std::max(max_end_time, cue.end_time());
        language_ids.push_back(cue.language_id);
        if (cue.text.size() > std::numeric_limits<uint32_t>::max() - cue.terminator_size) {
            return std::unexpected("USM build failed: subtitle cue text is too large");
        }
        max_record_size = std::max<uint32_t>(
            max_record_size,
            static_cast<uint32_t>(cue.text.size() + cue.terminator_size + 0x14u)
        );
    }
    std::ranges::sort(language_ids);
    const auto last = std::ranges::unique(language_ids).begin();
    language_ids.erase(last, language_ids.end());

    info.total_time = max_end_time;
    info.channel_count = static_cast<uint32_t>(language_ids.size());
    info.ixsize = max_record_size;
    if (info.time_unit != 0 && info.total_time != 0) {
        const long double duration_seconds =
            static_cast<long double>(info.total_time) / static_cast<long double>(info.time_unit);
        if (duration_seconds > 0.0L) {
            info.avbps = static_cast<uint32_t>(std::llround(
                (static_cast<long double>(info.filesize) * 8.0L) / duration_seconds
            ));
        }
    }

    info.chunks.push_back(BuiltChunk{
        .chunk = make_chunk(UsmChunkType::SBT, UsmPayloadType::Stream, channel_no, 0, base_frame_rate, *payload),
        .priority = 2,
    });
    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SBT, channel_no, contents_end_marker),
        .priority = 2,
    });

    return info;
}

std::expected<VideoBuildInfo, std::string> build_ivf_video_chunks(
    const std::filesystem::path& path,
    const UsmCrypto* crypto,
    const text::EncodingOptions& encoding
) {
    video::IvfReader reader;
    if (auto result = reader.open(path); !result) {
        return std::unexpected(result.error());
    }

    const auto& header = reader.get_header();
    if (header.magic != IvfMagic.le_value() || header.fourcc != Vp9Magic.le_value()) {
        return std::unexpected("USM builder currently supports VP9 IVF input only");
    }

    VideoBuildInfo info;
    auto filename = encode_path_filename(path, encoding);
    if (!filename) {
        return std::unexpected(filename.error());
    }
    info.filename = *filename;
    info.filesize = static_cast<uint32_t>(std::filesystem::file_size(path));
    info.width = header.width;
    info.height = header.height;
    info.frame_count = header.num_frames;
    info.fmtver = vp9_fmtver;
    info.codec_id = vp9_codec_id;
    info.dcprec = vp9_dcprec;
    if (header.rate == 0 || header.scale == 0) {
        return std::unexpected("USM builder requires a non-zero IVF time base");
    }

    info.chunks.reserve(static_cast<size_t>(info.frame_count) + 1u);
    uint32_t max_chunk_size = 0;
    uint32_t actual_frame_count = 0;
    video::IvfTimeline timeline;
    while (reader.has_frames()) {
        auto frame = reader.read_next_frame();
        if (!frame) {
            if (frame.error() == "EOF") {
                break;
            }
            return std::unexpected(frame.error());
        }
        if (auto observed = timeline.observe(frame->timestamp); !observed) {
            return std::unexpected("USM builder rejected the VP9 timeline: " + observed.error());
        }
        auto frame_time = timeline.time_in(frame->timestamp, header.rate, header.scale, base_frame_rate);
        if (!frame_time) {
            return std::unexpected("USM builder rejected the VP9 timeline: " + frame_time.error());
        }

        std::vector<uint8_t> payload;
        if (actual_frame_count == 0) {
            const auto raw_header = reader.get_raw_header();
            payload.insert(payload.end(), raw_header.begin(), raw_header.end());
        }
        payload.insert(payload.end(), frame->record_bytes.begin(), frame->record_bytes.end());

        BuiltChunk chunk;
        chunk.priority = 0;
        chunk.is_keyframe = frame->is_keyframe;
        chunk.frame_index = actual_frame_count;
        chunk.chunk = make_chunk(
            UsmChunkType::SFV,
            UsmPayloadType::Stream,
            0,
            *frame_time,
            base_frame_rate,
            payload);
        transform_stream_chunk_payload_with_padding(chunk.chunk, crypto, false);
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.packed_size()));
        info.chunks.push_back(std::move(chunk));

        ++actual_frame_count;
    }

    info.frame_count = actual_frame_count;
    info.framerate_n = timeline.frame_rate_milli(header.rate, header.scale);
    if (info.frame_count != 0) {
        const long double duration_seconds = timeline.duration_seconds(header.rate, header.scale);
        if (duration_seconds > 0.0L) {
            info.avbps = static_cast<uint32_t>(std::llround(
                (static_cast<long double>(info.filesize) * 8.0L) / duration_seconds));
        }
    }
    info.minbuf = max_chunk_size;
    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFV, 0, contents_end_marker),
        .priority = 0,
    });

    return info;
}

std::expected<VideoBuildInfo, std::string> build_mpeg_video_chunks(
    const std::filesystem::path& path,
    const UsmCrypto* crypto,
    const text::EncodingOptions& encoding
) {
    video::MpegVideoReader reader;
    if (auto result = reader.open(path); !result) {
        return std::unexpected(result.error());
    }

    const auto& header = reader.sequence_header();
    const auto [fps_n, fps_d] = reader.frame_rate();

    VideoBuildInfo info;
    auto filename = encode_path_filename(path, encoding);
    if (!filename) {
        return std::unexpected(filename.error());
    }
    info.filename = *filename;
    info.filesize = static_cast<uint32_t>(std::filesystem::file_size(path));
    info.width = header.width;
    info.height = header.height;
    info.frame_count = reader.frame_count();
    info.framerate_n = fps_n;
    info.framerate_d = fps_d;
    info.fmtver = mpeg_fmtver;
    info.codec_id = mpeg1_codec_id;
    info.dcprec = mpeg_dcprec;

    const double frame_interval = fps_n != 0
        ? (static_cast<double>(base_frame_rate) * static_cast<double>(fps_d) / static_cast<double>(fps_n))
        : 99.9;
    double current_interval = 0.0;

    info.chunks.reserve(static_cast<size_t>(info.frame_count) + 1u);
    uint32_t max_chunk_size = 0;
    uint32_t actual_frame_count = 0;
    while (reader.has_frames()) {
        auto frame = reader.read_next_frame();
        if (!frame) {
            if (frame.error() == "EOF") {
                break;
            }
            return std::unexpected(frame.error());
        }

        std::vector<uint8_t> payload(frame->record_bytes.begin(), frame->record_bytes.end());
        BuiltChunk chunk;
        chunk.priority = 0;
        chunk.is_keyframe = frame->is_keyframe;
        chunk.frame_index = actual_frame_count;
        chunk.chunk = make_chunk(
            UsmChunkType::SFV,
            UsmPayloadType::Stream,
            0,
            static_cast<uint32_t>(std::llround(current_interval)),
            base_frame_rate,
            payload);
        transform_stream_chunk_payload_with_padding(chunk.chunk, crypto, false);
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.packed_size()));
        info.chunks.push_back(std::move(chunk));

        current_interval += frame_interval;
        ++actual_frame_count;
    }

    info.frame_count = actual_frame_count;
    if (fps_n != 0 && fps_d != 0 && info.frame_count != 0) {
        const double duration_seconds =
            (static_cast<double>(fps_d) * static_cast<double>(info.frame_count)) /
            static_cast<double>(fps_n);
        if (duration_seconds > 0.0) {
            info.avbps = static_cast<uint32_t>(std::llround(
                (static_cast<long double>(info.filesize) * 8.0L) / duration_seconds));
        }
    }
    info.minbuf = max_chunk_size;
    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFV, 0, contents_end_marker),
        .priority = 0,
    });

    return info;
}

std::expected<VideoBuildInfo, std::string> build_h264_video_chunks(
    const std::filesystem::path& path,
    const UsmCrypto* crypto,
    const text::EncodingOptions& encoding
) {
    video::H264VideoReader reader;
    if (auto result = reader.open(path); !result) {
        return std::unexpected(result.error());
    }

    const auto& sps = reader.sequence_parameter_set();
    const auto [fps_n, fps_d] = reader.frame_rate();

    VideoBuildInfo info;
    auto filename = encode_path_filename(path, encoding);
    if (!filename) {
        return std::unexpected(filename.error());
    }
    info.filename = *filename;
    info.filesize = static_cast<uint32_t>(std::filesystem::file_size(path));
    info.width = sps.width;
    info.height = sps.height;
    info.frame_count = reader.frame_count();
    info.fmtver = mpeg_fmtver;
    info.codec_id = h264_codec_id;
    info.dcprec = mpeg_dcprec;
    if (fps_n != 0 && fps_d != 0) {
        info.framerate_n = static_cast<uint32_t>(std::llround(
            (static_cast<long double>(fps_n) * 1000.0L) / static_cast<long double>(fps_d)));
        info.framerate_d = 1000;
    }

    const double frame_interval = fps_n != 0
        ? (static_cast<double>(base_frame_rate) * static_cast<double>(fps_d) / static_cast<double>(fps_n))
        : 99.9;
    double current_interval = 0.0;

    info.chunks.reserve(static_cast<size_t>(info.frame_count) + 1u);
    uint32_t max_chunk_size = 0;
    uint32_t actual_frame_count = 0;
    while (reader.has_frames()) {
        auto frame = reader.read_next_frame();
        if (!frame) {
            if (frame.error() == "EOF") {
                break;
            }
            return std::unexpected(frame.error());
        }

        std::vector<uint8_t> payload(frame->record_bytes.begin(), frame->record_bytes.end());
        BuiltChunk chunk;
        chunk.priority = 0;
        chunk.is_keyframe = frame->is_keyframe;
        chunk.frame_index = actual_frame_count;
        chunk.chunk = make_chunk(
            UsmChunkType::SFV,
            UsmPayloadType::Stream,
            0,
            static_cast<uint32_t>(std::llround(current_interval)),
            base_frame_rate,
            payload);
        transform_stream_chunk_payload_with_padding(chunk.chunk, crypto, false);
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.packed_size()));
        info.chunks.push_back(std::move(chunk));

        current_interval += frame_interval;
        ++actual_frame_count;
    }

    info.frame_count = actual_frame_count;
    if (fps_n != 0 && fps_d != 0 && info.frame_count != 0) {
        const double duration_seconds =
            (static_cast<double>(fps_d) * static_cast<double>(info.frame_count)) /
            static_cast<double>(fps_n);
        if (duration_seconds > 0.0) {
            info.avbps = static_cast<uint32_t>(std::llround(
                (static_cast<long double>(info.filesize) * 8.0L) / duration_seconds));
        }
    }
    info.minbuf = max_chunk_size;
    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFV, 0, contents_end_marker),
        .priority = 0,
    });

    return info;
}

std::expected<VideoBuildInfo, std::string> build_video_chunks(
    const std::filesystem::path& path,
    const UsmCrypto* crypto,
    const text::EncodingOptions& encoding
) {
    auto ivf = build_ivf_video_chunks(path, crypto, encoding);
    if (ivf) {
        return ivf;
    }

    auto mpeg = build_mpeg_video_chunks(path, crypto, encoding);
    if (mpeg) {
        return mpeg;
    }

    auto h264 = build_h264_video_chunks(path, crypto, encoding);
    if (h264) {
        return h264;
    }

    return std::unexpected(
        "USM builder supports VP9 IVF, MPEG/SFV, or H.264 Annex B elementary video input; IVF probe failed: " +
        ivf.error() + "; MPEG probe failed: " + mpeg.error() + "; H.264 probe failed: " + h264.error()
    );
}

std::expected<AudioBuildInfo, std::string> build_adx_audio_chunks(
    const std::filesystem::path& path,
    uint8_t channel_no,
    const UsmCrypto* crypto,
    bool encrypt_audio,
    const text::EncodingOptions& encoding
) {
    adx::AdxDecoder decoder;
    if (auto result = decoder.load(path.string()); !result) {
        return std::unexpected("USM build failed: could not inspect ADX audio: " + result.error());
    }

    io::reader reader;
    if (auto result = reader.open(path); !result) {
        return std::unexpected("USM build failed: could not open ADX audio: " + std::string(result.error()));
    }

    const auto& header = decoder.header();
    const auto file_bytes = reader.data();

    AudioBuildInfo info;
    auto filename = encode_path_filename(path, encoding);
    if (!filename) {
        return std::unexpected(filename.error());
    }
    info.filename = *filename;
    info.filesize = static_cast<uint32_t>(file_bytes.size());
    info.sample_rate = header.sample_rate;
    info.total_samples = header.sample_count;
    info.channels = header.channels;
    info.channel_no = channel_no;

    if (info.sample_rate != 0 && info.total_samples != 0) {
        const double duration_seconds =
            static_cast<double>(info.total_samples) / static_cast<double>(info.sample_rate);
        if (duration_seconds > 0.0) {
            info.avbps = static_cast<uint32_t>(std::llround(
                (static_cast<long double>(file_bytes.size()) * 8.0L) / duration_seconds));
        }
    }

    const size_t first_chunk_size = std::min<size_t>(file_bytes.size(), header.data_offset + 4u);
    const size_t steady_chunk_size = static_cast<size_t>(
        (info.sample_rate / (static_cast<double>(base_frame_rate) / 100.0) / 32.0) *
        (header.block_size * header.channels));
    size_t offset = 0;
    double current_interval = 0.0;

    while (offset < file_bytes.size()) {
        size_t chunk_size = offset == 0 ? first_chunk_size : steady_chunk_size;
        if (chunk_size == 0 || offset + chunk_size > file_bytes.size()) {
            chunk_size = file_bytes.size() - offset;
        }
        if (chunk_size == 0) {
            break;
        }

        std::vector<uint8_t> payload(
            file_bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            file_bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
        const auto frame_time = static_cast<uint32_t>(std::llround(current_interval));
        BuiltChunk chunk{
            .chunk = make_chunk(
                UsmChunkType::SFA,
                UsmPayloadType::Stream,
                channel_no,
                frame_time,
                base_frame_rate,
                payload),
            .priority = 1,
        };
        if (encrypt_audio) {
            transform_stream_chunk_payload_with_padding(chunk.chunk, crypto, true);
        }
        info.chunks.push_back(std::move(chunk));

        offset += chunk_size;
        current_interval += audio_chunk_interval;
    }

    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFA, channel_no, contents_end_marker),
        .priority = 1,
    });

    return info;
}

UsmChunk build_hca_metadata_chunk(
    std::span<const uint8_t> header,
    uint8_t channel_no
) {
    utf::UtfTable table = utf::UtfTable::create("AUDIO_HEADER");
    table.add_column("hca_header", utf::ColumnType::VLData);

    const auto row = table.add_row();
    table.set(row, "hca_header", std::vector<uint8_t>(header.begin(), header.end())).value();
    const auto payload = table.build();
    return make_chunk(UsmChunkType::SFA, UsmPayloadType::Metadata, channel_no, 0, 30, payload);
}

std::expected<AudioBuildInfo, std::string> build_hca_audio_chunks(
    const std::filesystem::path& path,
    const hca::Hca& source,
    uint8_t channel_no,
    bool encrypt_audio,
    uint64_t key,
    const text::EncodingOptions& encoding
) {
    auto source_bytes = source.bytes();
    if (!source_bytes) {
        return std::unexpected("USM build failed: could not read HCA audio: " + source_bytes.error());
    }

    std::vector<uint8_t> bytes = std::move(*source_bytes);
    if (encrypt_audio && !source.header().cipher.encrypted()) {
        if (key == 0) {
            return std::unexpected("USM build failed: HCA audio encryption requires a nonzero key");
        }
        auto encrypted = hca::encrypt(bytes, 56, key);
        if (!encrypted) {
            return std::unexpected("USM build failed: could not encrypt HCA audio: " + encrypted.error());
        }
        bytes = std::move(*encrypted);
    }

    auto prepared = hca::Hca::load(std::span<const uint8_t>(bytes));
    if (!prepared) {
        return std::unexpected("USM build failed: prepared HCA audio is invalid: " + prepared.error());
    }
    const auto& header = prepared->header();
    if (header.codec.frame_size == 0 || header.fmt.sample_rate == 0) {
        return std::unexpected("USM build failed: HCA audio has an invalid frame layout");
    }

    const uint64_t expected_size = static_cast<uint64_t>(header.file.header_size) +
        static_cast<uint64_t>(header.fmt.frame_count) * header.codec.frame_size;
    if (expected_size != bytes.size()) {
        return std::unexpected("USM build failed: HCA audio size differs from its declared frame layout");
    }
    if (bytes.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("USM build failed: HCA audio is too large for USM metadata");
    }

    AudioBuildInfo info;
    auto filename = encode_path_filename(path, encoding);
    if (!filename) {
        return std::unexpected(filename.error());
    }
    info.filename = *filename;
    info.filesize = static_cast<uint32_t>(bytes.size());
    info.sample_rate = header.fmt.sample_rate;
    info.total_samples = header.sample_count();
    info.channels = static_cast<uint8_t>(header.fmt.channel_count);
    info.channel_no = channel_no;
    info.codec = UsmAudioCodec::Hca;
    if (info.total_samples != 0) {
        const uint64_t bits_per_second =
            static_cast<uint64_t>(info.filesize) * 8u * info.sample_rate / info.total_samples;
        info.avbps = static_cast<uint32_t>(std::min<uint64_t>(
            bits_per_second,
            std::numeric_limits<uint32_t>::max()
        ));
    }

    const auto hca_header = std::span<const uint8_t>(bytes).first(header.file.header_size);
    info.metadata_chunks.push_back(build_hca_metadata_chunk(hca_header, channel_no));
    info.chunks.push_back(BuiltChunk{
        .chunk = make_chunk(
            UsmChunkType::SFA,
            UsmPayloadType::Stream,
            channel_no,
            0,
            base_frame_rate,
            hca_header),
        .priority = 1,
    });

    for (uint32_t frame_index = 0; frame_index < header.fmt.frame_count; ++frame_index) {
        const uint64_t timestamp_numerator =
            static_cast<uint64_t>(frame_index) * hca::HCA_SAMPLES_PER_FRAME * base_frame_rate;
        const uint64_t frame_time =
            (timestamp_numerator + header.fmt.sample_rate / 2u) / header.fmt.sample_rate;
        if (frame_time > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected("USM build failed: HCA frame timestamp exceeds the USM field width");
        }

        const size_t frame_offset = static_cast<size_t>(header.file.header_size) +
            static_cast<size_t>(frame_index) * header.codec.frame_size;
        info.chunks.push_back(BuiltChunk{
            .chunk = make_chunk(
                UsmChunkType::SFA,
                UsmPayloadType::Stream,
                channel_no,
                static_cast<uint32_t>(frame_time),
                base_frame_rate,
                std::span<const uint8_t>(bytes).subspan(frame_offset, header.codec.frame_size)),
            .priority = 1,
        });
    }

    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFA, channel_no, contents_end_marker),
        .priority = 1,
    });
    return info;
}

std::expected<AudioBuildInfo, std::string> build_audio_chunks(
    const UsmBuildInput::AudioTrack& track,
    UsmAudioCodec codec,
    uint8_t channel_no,
    const UsmCrypto* crypto,
    bool encrypt_audio,
    uint64_t key,
    const text::EncodingOptions& encoding
) {
    if (codec == UsmAudioCodec::Hca) {
        auto hca_source = hca::Hca::load(track.path);
        if (!hca_source) {
            return std::unexpected("USM build failed: could not inspect HCA audio: " + hca_source.error());
        }
        return build_hca_audio_chunks(
            track.path,
            *hca_source,
            channel_no,
            encrypt_audio,
            key,
            encoding
        );
    }
    if (codec == UsmAudioCodec::Adx) {
        return build_adx_audio_chunks(track.path, channel_no, crypto, encrypt_audio, encoding);
    }
    return std::unexpected("USM build failed: unsupported audio codec");
}

UsmChunk build_video_seekinfo_chunk(std::span<const VideoSeekEntry> seek_entries) {
    utf::UtfTable table = utf::UtfTable::create("VIDEO_SEEKINFO");
    table.add_column("ofs_byte", utf::ColumnType::UInt64);
    table.add_column("ofs_frmid", utf::ColumnType::SInt32);
    table.add_column("num_skip", utf::ColumnType::SInt16);
    table.add_column("resv", utf::ColumnType::SInt16);

    for (const auto& entry : seek_entries) {
        const auto row = table.add_row();
        table.set(row, "ofs_byte", entry.byte_offset).value();
        table.set(row, "ofs_frmid", static_cast<int32_t>(entry.frame_index)).value();
        table.set(row, "num_skip", static_cast<int16_t>(0)).value();
        table.set(row, "resv", static_cast<int16_t>(0)).value();
    }

    const auto payload = table.build();
    return make_chunk(UsmChunkType::SFV, UsmPayloadType::Metadata, 0, 0, 30, payload);
}

UsmChunk build_video_header_chunk(
    const VideoBuildInfo& video,
    uint32_t metadata_count,
    uint32_t metadata_size
) {
    utf::UtfTable table = utf::UtfTable::create("VIDEO_HDRINFO");
    table.add_column("width", utf::ColumnType::UInt32);
    table.add_column("height", utf::ColumnType::UInt32);
    table.add_column("mat_width", utf::ColumnType::UInt32);
    table.add_column("mat_height", utf::ColumnType::UInt32);
    table.add_column("disp_width", utf::ColumnType::UInt32);
    table.add_column("disp_height", utf::ColumnType::UInt32);
    table.add_column("scrn_width", utf::ColumnType::UInt32);
    table.add_column("mpeg_dcprec", utf::ColumnType::UInt8);
    table.add_column("mpeg_codec", utf::ColumnType::UInt8);
    table.add_column("alpha_type", utf::ColumnType::UInt32);
    table.add_column("total_frames", utf::ColumnType::UInt32);
    table.add_column("framerate_n", utf::ColumnType::UInt32);
    table.add_column("framerate_d", utf::ColumnType::UInt32);
    table.add_column("metadata_count", utf::ColumnType::UInt32);
    table.add_column("metadata_size", utf::ColumnType::UInt32);
    table.add_column("ixsize", utf::ColumnType::UInt32);
    table.add_column("pre_padding", utf::ColumnType::UInt32);
    table.add_column("max_picture_size", utf::ColumnType::UInt32);
    table.add_column("color_space", utf::ColumnType::UInt32);
    table.add_column("picture_type", utf::ColumnType::UInt32);

    const auto row = table.add_row();
    table.set(row, "width", video.width).value();
    table.set(row, "height", video.height).value();
    table.set(row, "mat_width", video.width).value();
    table.set(row, "mat_height", video.height).value();
    table.set(row, "disp_width", video.width).value();
    table.set(row, "disp_height", video.height).value();
    table.set(row, "scrn_width", 0u).value();
    table.set(row, "mpeg_dcprec", video.dcprec).value();
    table.set(row, "mpeg_codec", static_cast<uint8_t>(video.codec_id)).value();
    table.set(row, "alpha_type", 0u).value();
    table.set(row, "total_frames", video.frame_count).value();
    table.set(row, "framerate_n", video.framerate_n).value();
    table.set(row, "framerate_d", video.framerate_d).value();
    table.set(row, "metadata_count", metadata_count).value();
    table.set(row, "metadata_size", metadata_size).value();
    table.set(row, "ixsize", video.minbuf).value();
    table.set(row, "pre_padding", 0u).value();
    table.set(row, "max_picture_size", 0u).value();
    table.set(row, "color_space", 0u).value();
    table.set(row, "picture_type", 0u).value();
    const auto payload = table.build();
    return make_chunk(UsmChunkType::SFV, UsmPayloadType::Header, 0, 0, 30, payload);
}

UsmChunk build_audio_header_chunk(const AudioBuildInfo& audio, uint8_t channel_no) {
    utf::UtfTable table = utf::UtfTable::create("AUDIO_HDRINFO");
    table.add_column("audio_codec", utf::ColumnType::UInt8);
    table.add_column("sampling_rate", utf::ColumnType::UInt32);
    table.add_column("total_samples", utf::ColumnType::UInt32);
    table.add_column("num_channels", utf::ColumnType::UInt8);
    table.add_column("metadata_count", utf::ColumnType::UInt32);
    table.add_column("metadata_size", utf::ColumnType::UInt32);
    table.add_column("ixsize", utf::ColumnType::UInt32);
    table.add_column("ambisonics", utf::ColumnType::UInt32);

    const auto row = table.add_row();
    uint32_t metadata_size = 0;
    for (const auto& chunk : audio.metadata_chunks) {
        metadata_size += static_cast<uint32_t>(chunk.packed_size());
    }

    table.set(row, "audio_codec", static_cast<uint8_t>(audio.codec)).value();
    table.set(row, "sampling_rate", audio.sample_rate).value();
    table.set(row, "total_samples", audio.total_samples).value();
    table.set(row, "num_channels", audio.channels).value();
    table.set(row, "metadata_count", static_cast<uint32_t>(audio.metadata_chunks.size())).value();
    table.set(row, "metadata_size", metadata_size).value();
    table.set(row, "ixsize", audio_ixsize).value();
    table.set(row, "ambisonics", 0u).value();
    const auto payload = table.build();
    return make_chunk(UsmChunkType::SFA, UsmPayloadType::Header, channel_no, 0, 30, payload);
}

UsmChunk build_subtitle_header_chunk(const SubtitleBuildInfo& subtitle, uint8_t channel_no) {
    utf::UtfTable table = utf::UtfTable::create("SUBTITLE_HDRINFO");
    table.add_column("time_unit", utf::ColumnType::UInt32);
    table.add_column("total_time", utf::ColumnType::UInt32);
    table.add_column("num_channels", utf::ColumnType::UInt32);
    table.add_column("content_xsize", utf::ColumnType::UInt32);
    table.add_column("ixsize", utf::ColumnType::UInt32);

    const auto row = table.add_row();
    table.set(row, "time_unit", subtitle.time_unit).value();
    table.set(row, "total_time", subtitle.total_time).value();
    table.set(row, "num_channels", subtitle.channel_count).value();
    table.set(row, "content_xsize", subtitle.content_size).value();
    table.set(row, "ixsize", subtitle.ixsize).value();
    const auto payload = table.build();
    return make_chunk(UsmChunkType::SBT, UsmPayloadType::Header, channel_no, 0, 30, payload);
}

UsmChunk build_crid_chunk(
    std::string_view usm_name,
    const VideoBuildInfo& video,
    const std::vector<AudioBuildInfo>& audios,
    const std::vector<SubtitleBuildInfo>& subtitles,
    uint32_t total_size
) {
    utf::UtfTable table = utf::UtfTable::create("CRIUSF_DIR_STREAM");
    table.add_column("fmtver", utf::ColumnType::UInt32);
    table.add_column("filename", utf::ColumnType::String);
    table.add_column("filesize", utf::ColumnType::UInt32);
    table.add_column("datasize", utf::ColumnType::UInt32);
    table.add_column("stmid", utf::ColumnType::UInt32);
    table.add_column("chno", utf::ColumnType::UInt16);
    table.add_column("minchk", utf::ColumnType::UInt16);
    table.add_column("minbuf", utf::ColumnType::UInt32);
    table.add_column("avbps", utf::ColumnType::UInt32);

    uint32_t total_avbps = video.avbps;
    uint32_t root_minbuf = 4 + video.minbuf;
    for (const auto& audio : audios) {
        total_avbps += audio.avbps;
        root_minbuf += audio_ixsize;
    }
    for (const auto& subtitle : subtitles) {
        total_avbps += subtitle.avbps;
        root_minbuf += subtitle.ixsize;
    }

    const auto root_row = table.add_row();
    table.set(root_row, "fmtver", video.fmtver).value();
    table.set(root_row, "filename", std::string(usm_name)).value();
    table.set(root_row, "filesize", total_size).value();
    table.set(root_row, "datasize", 0u).value();
    table.set(root_row, "stmid", 0u).value();
    table.set(root_row, "chno", static_cast<uint16_t>(0xFFFF)).value();
    table.set(root_row, "minchk", static_cast<uint16_t>(1)).value();
    table.set(root_row, "minbuf", root_minbuf).value();
    table.set(root_row, "avbps", total_avbps).value();
    const auto video_row = table.add_row();
    table.set(video_row, "fmtver", video.fmtver).value();
    table.set(video_row, "filename", video.filename).value();
    table.set(video_row, "filesize", video.filesize).value();
    table.set(video_row, "datasize", 0u).value();
    table.set(video_row, "stmid", static_cast<uint32_t>(UsmChunkType::SFV)).value();
    table.set(video_row, "chno", static_cast<uint16_t>(0)).value();
    table.set(video_row, "minchk", static_cast<uint16_t>(3)).value();
    table.set(video_row, "minbuf", video.minbuf).value();
    table.set(video_row, "avbps", video.avbps).value();
    for (size_t index = 0; index < audios.size(); ++index) {
        const auto& audio = audios[index];
        const auto row = table.add_row();
        table.set(row, "fmtver", video.fmtver).value();
        table.set(row, "filename", audio.filename).value();
        table.set(row, "filesize", audio.filesize).value();
        table.set(row, "datasize", 0u).value();
        table.set(row, "stmid", static_cast<uint32_t>(UsmChunkType::SFA)).value();
        table.set(row, "chno", static_cast<uint16_t>(audio.channel_no)).value();
        table.set(row, "minchk", static_cast<uint16_t>(1)).value();
        table.set(row, "minbuf", audio_ixsize).value();
        table.set(row, "avbps", audio.avbps).value();
    }

    for (size_t index = 0; index < subtitles.size(); ++index) {
        const auto& subtitle = subtitles[index];
        const auto row = table.add_row();
        table.set(row, "fmtver", video.fmtver).value();
        table.set(row, "filename", subtitle.filename).value();
        table.set(row, "filesize", subtitle.filesize).value();
        table.set(row, "datasize", 0u).value();
        table.set(row, "stmid", static_cast<uint32_t>(UsmChunkType::SBT)).value();
        table.set(row, "chno", static_cast<uint16_t>(subtitle.channel_no)).value();
        table.set(row, "minchk", static_cast<uint16_t>(1)).value();
        table.set(row, "minbuf", subtitle.ixsize).value();
        table.set(row, "avbps", subtitle.avbps).value();
    }

    auto payload = table.build();
    constexpr uint32_t crid_total_size = 0x800;
    constexpr uint32_t crid_body_size = crid_total_size - UsmChunkHeader::raw_header_size;
    if (payload.size() > crid_body_size) {
        return {};
    }

    UsmChunkHeader header;
    header.magic = static_cast<uint32_t>(UsmChunkType::CRID);
    header.chunk_size = crid_total_size - 0x08;
    header.offset = 0x18;
    header.padding = static_cast<uint16_t>(crid_body_size - payload.size());
    header.type = static_cast<uint8_t>(UsmPayloadType::Header);
    header.frame_rate = 30;

    return UsmChunk{
        .header = header,
        .payload = std::vector<uint8_t>(payload.begin(), payload.end()),
        .padding = {},
    };
}

std::expected<std::vector<uint8_t>, std::string> build_impl(
    UsmCrypto& crypto,
    const UsmBuildInput& input,
    std::string_view usm_name
) {
    auto plan = plan_build_impl(input);
    if (!plan) {
        return std::unexpected(plan.error());
    }

    crypto.clear_key();
    if (input.key != 0) {
        crypto.init_key(input.key);
    }

    auto video = build_video_chunks(input.video_path, &crypto, input.encoding);
    if (!video) {
        return std::unexpected(video.error());
    }

    std::vector<AudioBuildInfo> audios;
    audios.reserve(input.audio_tracks.size());
    for (size_t index = 0; index < input.audio_tracks.size(); ++index) {
        const auto& track = input.audio_tracks[index];
        const auto& planned_track = plan->audio_tracks[index];
        auto audio = build_audio_chunks(
            track,
            planned_track.codec,
            planned_track.channel_no,
            &crypto,
            planned_track.encrypt,
            input.key,
            input.encoding);
        if (!audio) {
            return std::unexpected(audio.error());
        }
        audios.push_back(std::move(*audio));
    }

    std::vector<SubtitleBuildInfo> subtitles;
    subtitles.reserve(input.subtitle_tracks.size());
    for (size_t index = 0; index < input.subtitle_tracks.size(); ++index) {
        auto subtitle = build_subtitle_chunks(
            input.subtitle_tracks[index],
            plan->subtitle_tracks[index].channel_no,
            input.encoding
        );
        if (!subtitle) {
            return std::unexpected(subtitle.error());
        }
        subtitles.push_back(std::move(*subtitle));
    }

    std::vector<VideoSeekEntry> placeholder_seek_entries;
    placeholder_seek_entries.reserve(video->chunks.size());
    for (const auto& chunk : video->chunks) {
        if (chunk.is_keyframe) {
            placeholder_seek_entries.push_back(VideoSeekEntry{
                .byte_offset = std::numeric_limits<uint64_t>::max() - placeholder_seek_entries.size(),
                .frame_index = chunk.frame_index,
            });
        }
    }

    const auto video_seek_chunk_placeholder = placeholder_seek_entries.empty()
        ? UsmChunk{}
        : build_video_seekinfo_chunk(placeholder_seek_entries);
    const auto video_metadata_count = placeholder_seek_entries.empty() ? 0u : 1u;
    const auto video_metadata_size = placeholder_seek_entries.empty()
        ? 0u
        : static_cast<uint32_t>(video_seek_chunk_placeholder.packed_size());

    std::vector<UsmChunk> prestream_chunks;
    const auto video_header_chunk = build_video_header_chunk(*video, video_metadata_count, video_metadata_size);
    prestream_chunks.push_back(video_header_chunk);
    for (const auto& audio : audios) {
        prestream_chunks.push_back(build_audio_header_chunk(audio, audio.channel_no));
    }
    for (const auto& subtitle : subtitles) {
        prestream_chunks.push_back(build_subtitle_header_chunk(subtitle, subtitle.channel_no));
    }
    const auto video_header_end = make_end_chunk(UsmChunkType::SFV, 0, header_end_marker);
    prestream_chunks.push_back(video_header_end);
    for (const auto& audio : audios) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SFA, audio.channel_no, header_end_marker));
    }
    for (const auto& subtitle : subtitles) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SBT, subtitle.channel_no, header_end_marker));
    }
    const auto seek_chunk_insert_index = prestream_chunks.size();
    if (video_metadata_count != 0u) {
        prestream_chunks.push_back(video_seek_chunk_placeholder);
    }
    for (const auto& audio : audios) {
        prestream_chunks.insert(
            prestream_chunks.end(),
            audio.metadata_chunks.begin(),
            audio.metadata_chunks.end()
        );
    }
    const auto video_metadata_end = make_end_chunk(UsmChunkType::SFV, 0, metadata_end_marker);
    prestream_chunks.push_back(video_metadata_end);
    for (const auto& audio : audios) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SFA, audio.channel_no, metadata_end_marker));
    }
    for (const auto& subtitle : subtitles) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SBT, subtitle.channel_no, metadata_end_marker));
    }

    size_t content_chunk_count = video->chunks.size();
    for (const auto& audio : audios) {
        content_chunk_count += audio.chunks.size();
    }
    for (const auto& subtitle : subtitles) {
        content_chunk_count += subtitle.chunks.size();
    }

    std::vector<BuiltChunk> content_chunks;
    content_chunks.reserve(content_chunk_count);
    content_chunks.insert(
        content_chunks.end(),
        std::make_move_iterator(video->chunks.begin()),
        std::make_move_iterator(video->chunks.end())
    );
    for (size_t index = 0; index < audios.size(); ++index) {
        content_chunks.insert(
            content_chunks.end(),
            std::make_move_iterator(audios[index].chunks.begin()),
            std::make_move_iterator(audios[index].chunks.end())
        );
    }
    for (size_t index = 0; index < subtitles.size(); ++index) {
        content_chunks.insert(
            content_chunks.end(),
            std::make_move_iterator(subtitles[index].chunks.begin()),
            std::make_move_iterator(subtitles[index].chunks.end())
        );
    }
    std::stable_sort(content_chunks.begin(), content_chunks.end(), [](const BuiltChunk& lhs, const BuiltChunk& rhs) {
        return std::tie(lhs.chunk.header.type, lhs.chunk.header.frame_time, lhs.priority) <
            std::tie(rhs.chunk.header.type, rhs.chunk.header.frame_time, rhs.priority);
    });

    size_t header_size = 0;
    for (const auto& chunk : prestream_chunks) {
        header_size += chunk.packed_size();
    }

    size_t content_size = 0;
    for (const auto& chunk : content_chunks) {
        content_size += chunk.chunk.packed_size();
    }

    if (!placeholder_seek_entries.empty()) {
        std::vector<VideoSeekEntry> seek_entries;
        seek_entries.reserve(placeholder_seek_entries.size());

        uint64_t current_chunk_offset = 0x800ull + static_cast<uint64_t>(header_size);
        for (const auto& chunk : content_chunks) {
            if (chunk.chunk.header.magic == static_cast<uint32_t>(UsmChunkType::SFV) &&
                chunk.chunk.payload_type() == UsmPayloadType::Stream &&
                chunk.chunk.header.channel_no == 0 &&
                chunk.is_keyframe) {
                seek_entries.push_back(VideoSeekEntry{
                    .byte_offset = current_chunk_offset,
                    .frame_index = chunk.frame_index,
                });
            }
            current_chunk_offset += chunk.chunk.packed_size();
        }

        if (seek_entries.size() != placeholder_seek_entries.size()) {
            return std::unexpected("USM build failed: could not map video keyframes to built SFV chunks");
        }

        const auto video_seek_chunk = build_video_seekinfo_chunk(seek_entries);
        if (video_seek_chunk.packed_size() != video_seek_chunk_placeholder.packed_size()) {
            return std::unexpected("USM build failed: VIDEO_SEEKINFO chunk size changed unexpectedly");
        }
        prestream_chunks[seek_chunk_insert_index] = video_seek_chunk;
    }

    header_size = 0;
    for (const auto& chunk : prestream_chunks) {
        header_size += chunk.packed_size();
    }

    const size_t total_size_size_t = 0x800ull + header_size + content_size;
    if (total_size_size_t > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected("USM build failed: output is too large for the current metadata layout");
    }
    const auto total_size = static_cast<uint32_t>(total_size_size_t);
    auto encoded_usm_name = text::encode_cri_string(usm_name, input.encoding);
    if (!encoded_usm_name) {
        return std::unexpected("USM build failed: could not encode CRID container filename: " + encoded_usm_name.error());
    }
    const std::string crid_name(encoded_usm_name->begin(), encoded_usm_name->end());
    const auto crid_chunk = build_crid_chunk(crid_name, *video, audios, subtitles, total_size);
    if (crid_chunk.header.magic == 0) {
        return std::unexpected("USM build failed: could not build CRID metadata chunk");
    }

    std::vector<uint8_t> output;
    output.reserve(total_size);
    crid_chunk.append_to(output);
    for (const auto& chunk : prestream_chunks) {
        chunk.append_to(output);
    }
    for (const auto& chunk : content_chunks) {
        chunk.chunk.append_to(output);
    }

    return output;
}

} // namespace

std::expected<UsmBuildPlan, std::string> plan_build(const UsmBuildInput& input) {
    return plan_build_impl(input);
}

std::expected<std::vector<uint8_t>, std::string> UsmBuilder::build(const UsmBuildInput& input) {
    const auto default_name = input.video_path.empty()
        ? std::string("output.usm")
        : input.video_path.stem().string() + ".usm";
    return build_impl(m_crypto, input, default_name);
}

std::expected<void, std::string> UsmBuilder::build_to_file(
    const std::filesystem::path& output_path,
    const UsmBuildInput& input
) {
    auto buffer = build_impl(m_crypto, input, output_path.filename().string());
    if (!buffer) {
        return std::unexpected(buffer.error());
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("USM build failed: could not open output file: " + std::string(result.error()));
    }
    if (auto result = writer.write(*buffer); !result) {
        return std::unexpected("USM build failed: could not write output file: " + std::string(result.error()));
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("USM build failed: could not finalize output file: " + std::string(result.error()));
    }

    return {};
}

} // namespace cricodecs::usm
