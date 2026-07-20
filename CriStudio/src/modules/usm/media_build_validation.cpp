#include "modules/usm/media_build_validation.hpp"

#include "h264.hpp"
#include "ivf.hpp"
#include "mpeg.hpp"
#include "wav_container.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace cristudio::modules::usm {
namespace {

constexpr uint32_t vp9_fourcc =
    static_cast<uint32_t>('V') |
    (static_cast<uint32_t>('P') << 8u) |
    (static_cast<uint32_t>('9') << 16u) |
    (static_cast<uint32_t>('0') << 24u);

uint64_t duration_ms(uint64_t units, uint64_t rate, uint64_t scale = 1) {
    if (units == 0 || rate == 0 || scale == 0 ||
        units > (std::numeric_limits<uint64_t>::max)() / 1000u / scale) {
        return 0;
    }
    return units * 1000u * scale / rate;
}

std::expected<PreparedVideoInfo, std::string> inspect_ivf(
    const std::filesystem::path& path,
    bool require_vp9_payloads
) {
    cricodecs::video::IvfReader reader;
    if (auto opened = reader.open(path); !opened) {
        return std::unexpected(opened.error());
    }

    const auto& header = reader.get_header();
    if (header.header_size != 32u) {
        return std::unexpected("IVF header size is not 32 bytes");
    }
    if (require_vp9_payloads && header.fourcc != vp9_fourcc) {
        return std::unexpected("expected VP9 in an IVF container");
    }
    if (header.width == 0 || header.height == 0 || header.rate == 0 || header.scale == 0) {
        return std::unexpected("IVF dimensions or frame rate are zero");
    }

    uint32_t frames = 0;
    uint64_t parsed_size = header.header_size;
    while (reader.has_frames()) {
        auto frame = reader.read_next_frame();
        if (!frame) {
            return std::unexpected(frame.error());
        }
        if (require_vp9_payloads && !cricodecs::video::is_valid_vp9_frame(frame->data)) {
            return std::unexpected("IVF contains an invalid VP9 frame at index " + std::to_string(frames));
        }
        parsed_size += frame->record_bytes.size();
        ++frames;
    }

    std::error_code file_error;
    const auto file_size = std::filesystem::file_size(path, file_error);
    if (file_error || parsed_size != file_size) {
        return std::unexpected("IVF has trailing or truncated frame data");
    }
    if (header.num_frames != frames) {
        return std::unexpected(
            "IVF header declares " + std::to_string(header.num_frames) +
            " frames but contains " + std::to_string(frames));
    }

    return PreparedVideoInfo{
        .frame_count = frames,
        .duration_ms = duration_ms(frames, header.rate, header.scale),
        .width = header.width,
        .height = header.height,
    };
}

std::expected<PreparedVideoInfo, std::string> inspect_h264(const std::filesystem::path& path) {
    cricodecs::video::H264VideoReader reader;
    if (auto opened = reader.open(path); !opened) {
        return std::unexpected(opened.error());
    }
    const auto structure = cricodecs::video::inspect_h264_structure(reader.data());
    if (structure.nal_units == 0 || structure.valid_nal_headers != structure.nal_units ||
        structure.valid_slice_headers < reader.frame_count() || structure.ebsp_violations != 0) {
        return std::unexpected("H.264 Annex B structure is malformed");
    }
    const auto& sps = reader.sequence_parameter_set();
    const auto [rate, scale] = reader.frame_rate();
    return PreparedVideoInfo{
        .frame_count = reader.frame_count(),
        .duration_ms = duration_ms(reader.frame_count(), rate, scale),
        .width = sps.width,
        .height = sps.height,
    };
}

std::expected<PreparedVideoInfo, std::string> inspect_mpeg(
    const std::filesystem::path& path,
    std::optional<cricodecs::video::MpegVideoType> expected_type
) {
    cricodecs::video::MpegVideoReader reader;
    if (auto opened = reader.open(path); !opened) {
        return std::unexpected(opened.error());
    }
    if (expected_type && reader.video_type() != *expected_type) {
        return std::unexpected(*expected_type == cricodecs::video::MpegVideoType::mpeg1
            ? "expected an MPEG-1 elementary stream"
            : "expected an MPEG-2 elementary stream");
    }
    const auto structure = cricodecs::video::inspect_mpeg_structure(reader.data());
    if (structure.pictures < reader.frame_count() || structure.slices == 0 ||
        structure.valid_start_codes != structure.start_codes || structure.violations != 0) {
        return std::unexpected("MPEG elementary-stream structure is malformed");
    }
    const auto& header = reader.sequence_header();
    const auto [rate, scale] = reader.frame_rate();
    return PreparedVideoInfo{
        .frame_count = reader.frame_count(),
        .duration_ms = duration_ms(reader.frame_count(), rate, scale),
        .width = header.width,
        .height = header.height,
    };
}

std::expected<void, std::string> validate_nontrivial_video(const PreparedVideoInfo& info) {
    if (info.frame_count == 0) {
        return std::unexpected(
            "the prepared stream contains no video frames. The input may be encrypted, damaged, or only partially decoded");
    }
    return {};
}

std::expected<void, std::string> validate_against_source(
    const PreparedVideoInfo& output,
    const PreparedVideoInfo& source
) {
    if (source.width != 0 && source.height != 0 &&
        (output.width != source.width || output.height != source.height)) {
        return std::unexpected(
            "output dimensions " + std::to_string(output.width) + "x" + std::to_string(output.height) +
            " do not match the recognizable source dimensions " + std::to_string(source.width) + "x" +
            std::to_string(source.height));
    }

    const bool frame_count_collapsed = source.frame_count > 1u &&
        static_cast<uint64_t>(output.frame_count) * 10u < static_cast<uint64_t>(source.frame_count) * 9u;
    constexpr uint64_t duration_tolerance_ms = 100;
    const bool duration_collapsed = source.duration_ms > duration_tolerance_ms &&
        (output.duration_ms + duration_tolerance_ms) * 10u < source.duration_ms * 9u;
    if (frame_count_collapsed || duration_collapsed) {
        return std::unexpected(
            "output is suspiciously truncated (" + std::to_string(output.frame_count) + " frames, " +
            std::to_string(output.duration_ms) + " ms; recognizable source has " +
            std::to_string(source.frame_count) + " frames, " + std::to_string(source.duration_ms) + " ms)");
    }
    return {};
}

} // namespace

std::optional<PreparedVideoInfo> inspect_video_source_hint(const std::filesystem::path& path) {
    if (auto ivf = inspect_ivf(path, false)) {
        return *ivf;
    }
    if (auto h264 = inspect_h264(path)) {
        return *h264;
    }
    if (auto mpeg = inspect_mpeg(path, std::nullopt)) {
        return *mpeg;
    }
    return std::nullopt;
}

std::expected<PreparedVideoInfo, std::string> validate_prepared_video_output(
    const std::filesystem::path& path,
    PreparedVideoFormat expected_format,
    const std::optional<PreparedVideoInfo>& source_hint
) {
    std::expected<PreparedVideoInfo, std::string> inspected = std::unexpected("unsupported prepared video format");
    switch (expected_format) {
    case PreparedVideoFormat::Vp9Ivf:
        inspected = inspect_ivf(path, true);
        break;
    case PreparedVideoFormat::H264AnnexB:
        inspected = inspect_h264(path);
        break;
    case PreparedVideoFormat::Mpeg1Elementary:
        inspected = inspect_mpeg(path, cricodecs::video::MpegVideoType::mpeg1);
        break;
    case PreparedVideoFormat::Mpeg2Elementary:
        inspected = inspect_mpeg(path, cricodecs::video::MpegVideoType::mpeg2);
        break;
    }
    if (!inspected) {
        return std::unexpected("FFmpeg video output rejected: " + inspected.error());
    }
    if (auto valid = validate_nontrivial_video(*inspected); !valid) {
        return std::unexpected("FFmpeg video output rejected: " + valid.error());
    }
    if (source_hint) {
        if (auto valid = validate_against_source(*inspected, *source_hint); !valid) {
            return std::unexpected("FFmpeg video output rejected: " + valid.error() +
                ". The input may be encrypted or damaged");
        }
    }
    return *inspected;
}

std::expected<PreparedAudioInfo, std::string> validate_prepared_pcm_wav(
    const std::filesystem::path& path
) {
    cricodecs::wav::WavContainer wav;
    if (auto loaded = wav.load(path); !loaded) {
        return std::unexpected("FFmpeg audio output rejected: " + loaded.error());
    }
    if (wav.format().compression_mode != 1u || wav.format().bit_depth != 16u) {
        return std::unexpected("FFmpeg audio output rejected: expected 16-bit PCM WAV");
    }
    if (auto pcm = wav.get_pcm16(); !pcm) {
        return std::unexpected("FFmpeg audio output rejected: " + pcm.error());
    }

    const auto samples = static_cast<uint64_t>(wav.sample_count());
    const auto rate = wav.sample_rate();
    const auto audio_duration_ms = duration_ms(samples, rate);
    if (samples == 0) {
        return std::unexpected(
            "FFmpeg audio output rejected: the prepared WAV contains no sample frames. "
            "The input may be encrypted, damaged, or only partially decoded");
    }
    return PreparedAudioInfo{
        .sample_count = samples,
        .duration_ms = audio_duration_ms,
        .sample_rate = rate,
        .channels = static_cast<uint16_t>(wav.channels()),
    };
}

} // namespace cristudio::modules::usm
