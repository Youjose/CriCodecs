/**
 * @file ivf.cpp
 * @brief IVF/VP9 parser for USM frame packaging.
 */

#include "ivf.hpp"

namespace cricodecs::video {

namespace {

bool is_vp9_keyframe(std::span<const uint8_t> frame_bytes) {
    if (frame_bytes.empty()) {
        return false;
    }

    io::bit_reader reader(frame_bytes);
    if (reader.remaining() < 6u) {
        return false;
    }

    const auto frame_marker = reader.read(2);
    if (frame_marker != 0x02) {
        return false;
    }

    const auto profile_low = reader.read(1);
    const auto profile_high = reader.read(1);
    const auto profile = profile_low | (profile_high << 1u);
    if (profile == 3u) {
        if (reader.remaining() < 1u) {
            return false;
        }
        const auto reserved_zero = reader.read(1);
        (void)reserved_zero;
    }

    if (reader.remaining() < 2u) {
        return false;
    }

    const auto show_existing_frame = reader.read(1);
    if (show_existing_frame != 0u) {
        return false;
    }

    const auto frame_type = reader.read(1);
    return frame_type == 0u;
}

} // namespace

std::expected<void, std::string> IvfReader::open(const std::filesystem::path& path) {
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("IVF load failed: could not open input file: " + path.string());
    }

    if (m_reader.size() < 32) {
        return std::unexpected("IVF parse failed: file is too small for header");
    }

    m_raw_header = m_reader.data().subspan(0, 32);
    m_reader.seek(0);

    m_header.magic = m_reader.read_le<uint32_t>();
    if (m_header.magic != 0x46494B44) { // DKIF
        return std::unexpected("IVF parse failed: invalid magic");
    }

    m_header.version = m_reader.read_le<uint16_t>();
    m_header.header_size = m_reader.read_le<uint16_t>();
    m_header.fourcc = m_reader.read_le<uint32_t>();
    m_header.width = m_reader.read_le<uint16_t>();
    m_header.height = m_reader.read_le<uint16_t>();
    m_header.rate = m_reader.read_le<uint32_t>();
    m_header.scale = m_reader.read_le<uint32_t>();
    m_header.num_frames = m_reader.read_le<uint32_t>();
    m_header.unused = m_reader.read_le<uint32_t>();

    m_current_frame = 0;
    return {};
}

bool IvfReader::has_frames() const {
    return m_reader.size() > 0 && (m_reader.size() - m_reader.tell()) >= 12;
}

std::expected<IvfFrame, std::string> IvfReader::read_next_frame() {
    if (m_reader.remaining() < 12) {
        return std::unexpected("IVF frame read failed: unexpected end of file before frame header");
    }

    const auto frame_offset = m_reader.tell();
    const uint32_t size = m_reader.read_le<uint32_t>();
    const uint64_t pts = m_reader.read_le<uint64_t>();

    IvfFrame frame;
    frame.size = size;
    frame.timestamp = pts;

    if (m_reader.remaining() < size) {
        return std::unexpected("IVF frame read failed: frame payload is truncated");
    }

    auto frame_data = m_reader.read_bytes(size);
    frame.data = frame_data;
    frame.is_keyframe = is_vp9_keyframe(frame.data);
    frame.record_bytes = m_reader.subspan(frame_offset, 12 + size);

    ++m_current_frame;
    return frame;
}

} // namespace cricodecs::video
