/**
 * @file usm_builder.cpp
 * @brief USM builder
 *
 * The mux layout started from PyCriCodecsEx behavior and has since been
 * checked against Medianoche/Sofdec 2 metadata names such as
 * CRIUSF_DIR_STREAM, VIDEO_HDRINFO, and VIDEO_SEEKINFO. The current builder
 * accepts VP9-in-IVF, MPEG/Sofdec elementary video, H.264 Annex B, and
 * optional ADX audio, with byte-exact stream reassembly as the primary
 * contract rather than byte-identical USM authoring.
 */

#include "usm_container.hpp"

#include "../adx/adx_codec.hpp"
#include "../video/h264.hpp"
#include "../video/mpeg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

#include "../utilities/numeric.hpp"

namespace cricodecs::usm {

namespace {

using cricodecs::util::align_up;

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
    uint32_t avbps = 0;
    std::vector<BuiltChunk> chunks;
};

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
    if (header.magic != 0x46494B44 || header.fourcc != 0x30395056) {
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
    if (header.rate != 0 && header.scale != 0) {
        info.framerate_n = static_cast<uint32_t>(std::llround(
            (static_cast<long double>(header.rate) * 1000.0L) / static_cast<long double>(header.scale)));
    }
    const double frame_interval = (header.rate != 0)
        ? (static_cast<double>(base_frame_rate) * static_cast<double>(header.scale) / static_cast<double>(header.rate))
        : 99.9;
    double current_interval = 0.0;

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

        std::vector<uint8_t> payload;
        if (actual_frame_count == 0) {
            const auto raw_header = reader.get_raw_header();
            payload.insert(payload.end(), raw_header.begin(), raw_header.end());
        }
        payload.insert(payload.end(), frame->record_bytes.begin(), frame->record_bytes.end());

        if (crypto != nullptr && crypto->has_key()) {
            payload = crypto->encrypt_video(payload);
        }

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
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.pack().size()));
        info.chunks.push_back(std::move(chunk));

        current_interval += frame_interval;
        ++actual_frame_count;
    }

    info.frame_count = actual_frame_count;
    if (header.rate != 0 && header.scale != 0 && info.frame_count != 0) {
        const double duration_seconds =
            (static_cast<double>(header.scale) * static_cast<double>(info.frame_count)) /
            static_cast<double>(header.rate);
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
        if (crypto != nullptr && crypto->has_key()) {
            payload = crypto->encrypt_video(payload);
        }

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
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.pack().size()));
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
        if (crypto != nullptr && crypto->has_key()) {
            payload = crypto->encrypt_video(payload);
        }

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
        max_chunk_size = std::max(max_chunk_size, static_cast<uint32_t>(chunk.chunk.pack().size()));
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
        if (crypto != nullptr && crypto->has_key() && encrypt_audio) {
            payload = crypto->encrypt_audio(payload);
        }

        const auto frame_time = static_cast<uint32_t>(std::llround(current_interval));
        info.chunks.push_back(BuiltChunk{
            .chunk = make_chunk(
                UsmChunkType::SFA,
                UsmPayloadType::Stream,
                channel_no,
                frame_time,
                base_frame_rate,
                payload),
            .priority = 1,
        });

        offset += chunk_size;
        current_interval += audio_chunk_interval;
    }

    info.chunks.push_back(BuiltChunk{
        .chunk = make_end_chunk(UsmChunkType::SFA, channel_no, contents_end_marker),
        .priority = 1,
    });

    return info;
}

UsmChunk build_video_seekinfo_chunk(std::span<const VideoSeekEntry> seek_entries) {
    utf::UtfTable table = utf::UtfTable::create("VIDEO_SEEKINFO");
    table.add_column("ofs_byte", utf::ColumnType::UInt64);
    table.add_column("ofs_frmid", utf::ColumnType::SInt32);
    table.add_column("num_skip", utf::ColumnType::SInt16);
    table.add_column("resv", utf::ColumnType::SInt16);

    for (const auto& entry : seek_entries) {
        const auto row = table.add_row();
        table.set(row, "ofs_byte", entry.byte_offset);
        table.set(row, "ofs_frmid", static_cast<int32_t>(entry.frame_index));
        table.set(row, "num_skip", static_cast<int16_t>(0));
        table.set(row, "resv", static_cast<int16_t>(0));
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
    table.set(row, "width", video.width);
    table.set(row, "height", video.height);
    table.set(row, "mat_width", video.width);
    table.set(row, "mat_height", video.height);
    table.set(row, "disp_width", video.width);
    table.set(row, "disp_height", video.height);
    table.set(row, "scrn_width", 0u);
    table.set(row, "mpeg_dcprec", video.dcprec);
    table.set(row, "mpeg_codec", static_cast<uint8_t>(video.codec_id));
    table.set(row, "alpha_type", 0u);
    table.set(row, "total_frames", video.frame_count);
    table.set(row, "framerate_n", video.framerate_n);
    table.set(row, "framerate_d", video.framerate_d);
    table.set(row, "metadata_count", metadata_count);
    table.set(row, "metadata_size", metadata_size);
    table.set(row, "ixsize", video.minbuf);
    table.set(row, "pre_padding", 0u);
    table.set(row, "max_picture_size", 0u);
    table.set(row, "color_space", 0u);
    table.set(row, "picture_type", 0u);

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
    table.set(row, "audio_codec", static_cast<uint8_t>(2));
    table.set(row, "sampling_rate", audio.sample_rate);
    table.set(row, "total_samples", audio.total_samples);
    table.set(row, "num_channels", audio.channels);
    table.set(row, "metadata_count", 0u);
    table.set(row, "metadata_size", 0u);
    table.set(row, "ixsize", audio_ixsize);
    table.set(row, "ambisonics", 0u);

    const auto payload = table.build();
    return make_chunk(UsmChunkType::SFA, UsmPayloadType::Header, channel_no, 0, 30, payload);
}

UsmChunk build_crid_chunk(
    std::string_view usm_name,
    const VideoBuildInfo& video,
    const std::vector<AudioBuildInfo>& audios,
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

    const auto root_row = table.add_row();
    table.set(root_row, "fmtver", video.fmtver);
    table.set(root_row, "filename", std::string(usm_name));
    table.set(root_row, "filesize", total_size);
    table.set(root_row, "datasize", 0u);
    table.set(root_row, "stmid", 0u);
    table.set(root_row, "chno", static_cast<uint16_t>(0xFFFF));
    table.set(root_row, "minchk", static_cast<uint16_t>(1));
    table.set(root_row, "minbuf", root_minbuf);
    table.set(root_row, "avbps", total_avbps);

    const auto video_row = table.add_row();
    table.set(video_row, "fmtver", video.fmtver);
    table.set(video_row, "filename", video.filename);
    table.set(video_row, "filesize", video.filesize);
    table.set(video_row, "datasize", 0u);
    table.set(video_row, "stmid", static_cast<uint32_t>(UsmChunkType::SFV));
    table.set(video_row, "chno", static_cast<uint16_t>(0));
    table.set(video_row, "minchk", static_cast<uint16_t>(3));
    table.set(video_row, "minbuf", video.minbuf);
    table.set(video_row, "avbps", video.avbps);

    for (size_t index = 0; index < audios.size(); ++index) {
        const auto& audio = audios[index];
        const auto row = table.add_row();
        table.set(row, "fmtver", video.fmtver);
        table.set(row, "filename", audio.filename);
        table.set(row, "filesize", audio.filesize);
        table.set(row, "datasize", 0u);
        table.set(row, "stmid", static_cast<uint32_t>(UsmChunkType::SFA));
        table.set(row, "chno", static_cast<uint16_t>(index));
        table.set(row, "minchk", static_cast<uint16_t>(1));
        table.set(row, "minbuf", audio_ixsize);
        table.set(row, "avbps", audio.avbps);
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
    if (input.video_path.empty()) {
        return std::unexpected("USM build requires a video input path");
    }

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
        auto audio = build_adx_audio_chunks(
            track.path,
            static_cast<uint8_t>(index),
            &crypto,
            track.encrypt || input.encrypt_audio,
            input.encoding);
        if (!audio) {
            return std::unexpected(audio.error());
        }
        audios.push_back(std::move(*audio));
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
        : static_cast<uint32_t>(video_seek_chunk_placeholder.pack().size());

    std::vector<UsmChunk> prestream_chunks;
    const auto video_header_chunk = build_video_header_chunk(*video, video_metadata_count, video_metadata_size);
    prestream_chunks.push_back(video_header_chunk);
    for (size_t index = 0; index < audios.size(); ++index) {
        prestream_chunks.push_back(build_audio_header_chunk(audios[index], static_cast<uint8_t>(index)));
    }
    const auto video_header_end = make_end_chunk(UsmChunkType::SFV, 0, header_end_marker);
    prestream_chunks.push_back(video_header_end);
    for (size_t index = 0; index < audios.size(); ++index) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SFA, static_cast<uint8_t>(index), header_end_marker));
    }
    const auto seek_chunk_insert_index = prestream_chunks.size();
    if (video_metadata_count != 0u) {
        prestream_chunks.push_back(video_seek_chunk_placeholder);
    }
    const auto video_metadata_end = make_end_chunk(UsmChunkType::SFV, 0, metadata_end_marker);
    prestream_chunks.push_back(video_metadata_end);
    for (size_t index = 0; index < audios.size(); ++index) {
        prestream_chunks.push_back(make_end_chunk(UsmChunkType::SFA, static_cast<uint8_t>(index), metadata_end_marker));
    }

    std::vector<BuiltChunk> content_chunks = video->chunks;
    for (size_t index = 0; index < audios.size(); ++index) {
        content_chunks.insert(content_chunks.end(), audios[index].chunks.begin(), audios[index].chunks.end());
    }
    std::sort(content_chunks.begin(), content_chunks.end(), [](const BuiltChunk& lhs, const BuiltChunk& rhs) {
        return std::tie(lhs.chunk.header.type, lhs.chunk.header.frame_time, lhs.priority) <
            std::tie(rhs.chunk.header.type, rhs.chunk.header.frame_time, rhs.priority);
    });

    size_t header_size = 0;
    for (const auto& chunk : prestream_chunks) {
        header_size += chunk.pack().size();
    }

    size_t content_size = 0;
    for (const auto& chunk : content_chunks) {
        content_size += chunk.chunk.pack().size();
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
            current_chunk_offset += chunk.chunk.pack().size();
        }

        if (seek_entries.size() != placeholder_seek_entries.size()) {
            return std::unexpected("USM build failed: could not map video keyframes to built SFV chunks");
        }

        const auto video_seek_chunk = build_video_seekinfo_chunk(seek_entries);
        if (video_seek_chunk.pack().size() != video_seek_chunk_placeholder.pack().size()) {
            return std::unexpected("USM build failed: VIDEO_SEEKINFO chunk size changed unexpectedly");
        }
        prestream_chunks[seek_chunk_insert_index] = video_seek_chunk;
    }

    header_size = 0;
    for (const auto& chunk : prestream_chunks) {
        header_size += chunk.pack().size();
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
    const auto crid_chunk = build_crid_chunk(crid_name, *video, audios, total_size);
    if (crid_chunk.header.magic == 0) {
        return std::unexpected("USM build failed: could not build CRID metadata chunk");
    }

    std::vector<uint8_t> output;
    output.reserve(total_size);
    const auto packed_crid = crid_chunk.pack();
    output.insert(output.end(), packed_crid.begin(), packed_crid.end());
    for (const auto& chunk : prestream_chunks) {
        const auto packed_chunk = chunk.pack();
        output.insert(output.end(), packed_chunk.begin(), packed_chunk.end());
    }
    for (const auto& chunk : content_chunks) {
        const auto packed_chunk = chunk.chunk.pack();
        output.insert(output.end(), packed_chunk.begin(), packed_chunk.end());
    }

    return output;
}

} // namespace

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
