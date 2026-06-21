/**
 * @file hca_reader.cpp
 * @brief HCA header parser.
 *
 * Header layout behavior is based on vgmstream/VGAudio readers and
 * cross-checked against CRI SDK parser/serializer paths.
 */

#include "hca_reader.hpp"

#include "hca_codec.hpp"
#include "hca_format.hpp"
#include "hca_tables.hpp"

#include <cstring>

#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::hca {

namespace {

using cricodecs::util::divide_round_up;
using BitReader = io::bit_reader;
using io::read_be;

} // namespace

std::expected<HcaHeader, std::string> detail::parse_header(std::span<const uint8_t> data) {
    if (data.size() < 8) {
        return std::unexpected(std::string("HCA parse failed: header is shorter than 8 bytes"));
    }

    HcaHeader info;
    BitReader br(data.data(), data.size());

    const uint32_t sig = br.read(32);
    if ((sig & HCA_MASK) != HCA_CHUNK_ID_HCA) {
        return std::unexpected(std::string("HCA parse failed: invalid HCA signature"));
    }

    info.file.version = static_cast<uint16_t>(br.read(16));
    info.file.header_size = static_cast<uint16_t>(br.read(16));
    if (info.file.header_size < 8 || data.size() < info.file.header_size) {
        return std::unexpected(std::string("HCA parse failed: invalid header size"));
    }

    if (tables::crc16_checksum(data.data(), info.file.header_size - 2) !=
        read_be<uint16_t>(data.data() + info.file.header_size - 2)) {
        return std::unexpected(std::string("HCA parse failed: header checksum mismatch"));
    }

    size_t remaining = info.file.header_size - 8;

    if (remaining >= 0x10 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_FMT) {
        br.skip(32);
        info.fmt.channel_count = static_cast<uint8_t>(br.read(8));
        info.fmt.sample_rate = br.read(24);
        info.fmt.frame_count = br.read(32);
        info.fmt.encoder_delay = static_cast<uint16_t>(br.read(16));
        info.fmt.encoder_padding = static_cast<uint16_t>(br.read(16));
        remaining -= 0x10;
    } else {
        return std::unexpected(std::string("HCA parse failed: missing fmt chunk"));
    }

    if (info.fmt.channel_count == 0 || info.fmt.channel_count > 8 || info.fmt.sample_rate == 0 || info.fmt.frame_count == 0) {
        return std::unexpected(std::string("HCA parse failed: invalid fmt chunk values"));
    }
    const uint64_t frame_sample_total = static_cast<uint64_t>(info.fmt.frame_count) * HCA_SAMPLES_PER_FRAME;
    if (static_cast<uint64_t>(info.fmt.encoder_delay) + info.fmt.encoder_padding >= frame_sample_total) {
        return std::unexpected(std::string("HCA parse failed: invalid encoder delay/padding"));
    }

    if (remaining >= 0x10 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_COMP) {
        br.skip(32);
        info.codec.set_type(HcaCodecChunkType::Comp);
        info.codec.frame_size = static_cast<uint16_t>(br.read(16));
        info.codec.min_resolution = static_cast<uint8_t>(br.read(8));
        info.codec.max_resolution = static_cast<uint8_t>(br.read(8));
        info.codec.track_count = static_cast<uint8_t>(br.read(8));
        info.codec.channel_config = static_cast<uint8_t>(br.read(8));
        info.codec.total_band_count = static_cast<uint8_t>(br.read(8));
        info.codec.base_band_count = static_cast<uint8_t>(br.read(8));
        info.codec.stereo_band_count = static_cast<uint8_t>(br.read(8));
        info.codec.bands_per_hfr_group = static_cast<uint8_t>(br.read(8));
        info.codec.set_ms_stereo(br.read(8) != 0);
        static_cast<void>(br.read(8));
        remaining -= 0x10;
    } else if (remaining >= 0x0C && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_DEC) {
        br.skip(32);
        info.codec.set_type(HcaCodecChunkType::Dec);
        info.codec.frame_size = static_cast<uint16_t>(br.read(16));
        info.codec.min_resolution = static_cast<uint8_t>(br.read(8));
        info.codec.max_resolution = static_cast<uint8_t>(br.read(8));
        info.codec.total_band_count = static_cast<uint8_t>(br.read(8) + 1);
        info.codec.base_band_count = static_cast<uint8_t>(br.read(8) + 1);
        info.codec.track_count = static_cast<uint8_t>(br.read(4));
        info.codec.channel_config = static_cast<uint8_t>(br.read(4));
        const uint8_t stereo_type = static_cast<uint8_t>(br.read(8));
        if (stereo_type == 0) {
            info.codec.base_band_count = info.codec.total_band_count;
        }
        info.codec.stereo_band_count = static_cast<uint8_t>(info.codec.total_band_count - info.codec.base_band_count);
        info.codec.bands_per_hfr_group = 0;
        info.codec.set_ms_stereo(false);
        remaining -= 0x0C;
    } else {
        return std::unexpected(std::string("HCA parse failed: missing comp/dec chunk"));
    }

    if (remaining >= 0x08 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_VBR) {
        br.skip(32);
        const uint16_t vbr_max_frame_size = static_cast<uint16_t>(br.read(16));
        info.vbr.max_frame_size = vbr_max_frame_size;
        info.vbr.noise_level = static_cast<uint16_t>(br.read(16));
        if (!(info.codec.frame_size == 0 && info.vbr.max_frame_size > HCA_MIN_FRAME_SIZE && info.vbr.max_frame_size <= 0x1FF)) {
            return std::unexpected(std::string("HCA parse failed: invalid vbr chunk"));
        }
        remaining -= 0x08;
    }

    if (remaining >= 0x06 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_ATH) {
        br.skip(32);
        info.ath.type = static_cast<uint16_t>(br.read(16));
        remaining -= 0x06;
    } else {
        info.ath.type = detail::default_ath_enabled_for_missing_chunk(info.file.version) ? 1u : 0u;
    }

    if (remaining >= 0x10 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_LOOP) {
        br.skip(32);
        info.loop.start_frame = br.read(32);
        info.loop.end_frame = br.read(32);
        info.loop.start_delay = static_cast<uint16_t>(br.read(16));
        info.loop.end_padding = static_cast<uint16_t>(br.read(16));
        if (info.loop.start_frame > info.loop.end_frame ||
            info.loop.end_frame >= info.fmt.frame_count ||
            info.loop.start_delay >= HCA_SAMPLES_PER_FRAME ||
            info.loop.end_padding >= HCA_SAMPLES_PER_FRAME) {
            return std::unexpected(std::string("HCA parse failed: invalid loop chunk"));
        }
        remaining -= 0x10;
    }

    if (remaining >= 0x06 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_CIPH) {
        br.skip(32);
        info.cipher.type = static_cast<uint16_t>(br.read(16));
        if (info.cipher.type != 0 && info.cipher.type != 1 && info.cipher.type != 56) {
            return std::unexpected(std::string("HCA parse failed: unsupported cipher type"));
        }
        remaining -= 0x06;
    }

    if (remaining >= 0x08 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_RVA) {
        br.skip(32);
        const uint32_t raw_volume = br.read(32);
        std::memcpy(&info.rva.volume, &raw_volume, sizeof(float));
        remaining -= 0x08;
    }

    if (remaining >= 0x05 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_COMM) {
        br.skip(32);
        info.comment.length = static_cast<uint8_t>(br.read(8));
        if (remaining < static_cast<size_t>(0x05 + info.comment.length)) {
            return std::unexpected(std::string("HCA parse failed: comment chunk exceeds header"));
        }
        br.skip(info.comment.length * 8);
        remaining -= static_cast<size_t>(0x05 + info.comment.length);
    }

    if (remaining >= 0x04 && (br.peek(32) & HCA_MASK) == HCA_CHUNK_ID_PAD) {
        remaining = 2;
    }

    if (info.codec.frame_size < HCA_MIN_FRAME_SIZE || info.codec.frame_size > HCA_MAX_FRAME_SIZE) {
        return std::unexpected(std::string("HCA parse failed: invalid frame size"));
    }

    if (info.file.version <= HCA_VERSION_V200) {
        if (info.codec.min_resolution != 1 || info.codec.max_resolution != 15) {
            return std::unexpected(std::string("HCA parse failed: invalid v1/v2 resolution bounds"));
        }
    } else if (info.codec.min_resolution > info.codec.max_resolution || info.codec.max_resolution > 15) {
        return std::unexpected(std::string("HCA parse failed: invalid resolution bounds"));
    }

    if (info.codec.track_count == 0) {
        info.codec.track_count = 1;
    }
    if (info.codec.track_count > info.fmt.channel_count) {
        return std::unexpected(std::string("HCA parse failed: track count exceeds channel count"));
    }
    if (info.codec.base_band_count + info.codec.stereo_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        info.codec.total_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        info.codec.bands_per_hfr_group > HCA_SAMPLES_PER_SUBFRAME) {
        return std::unexpected(std::string("HCA parse failed: invalid band counts"));
    }

    if (info.codec.bands_per_hfr_group > 0 && info.codec.total_band_count >= info.codec.base_band_count + info.codec.stereo_band_count) {
        const uint32_t hfr_band_count = info.codec.total_band_count - info.codec.base_band_count - info.codec.stereo_band_count;
        info.codec.hfr_group_count = static_cast<uint8_t>(
            divide_round_up(hfr_band_count, static_cast<uint32_t>(info.codec.bands_per_hfr_group))
        );
    }

    const uint64_t required_frame_bytes =
        static_cast<uint64_t>(info.file.header_size) +
        static_cast<uint64_t>(info.fmt.frame_count) * static_cast<uint64_t>(info.codec.frame_size);
    if (required_frame_bytes > data.size()) {
        return std::unexpected(std::string("HCA parse failed: frame payload is shorter than header declares"));
    }

    return info;
}

std::expected<HcaHeader, std::string> Hca::parse_header(std::span<const uint8_t> data) {
    return detail::parse_header(data);
}

} // namespace cricodecs::hca
