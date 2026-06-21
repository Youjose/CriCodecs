#pragma once

/**
 * @file ivf.hpp
 * @brief IVF/VP9 frame traversal used by the USM builder.
 *
 * This reader preserves the raw IVF file header and per-frame records so USM
 * muxing can round-trip VP9 streams as authored, while still exposing keyframe
 * markers needed for generated VIDEO_SEEKINFO metadata.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

#include "../utilities/io.hpp"

namespace cricodecs::video {

struct IvfHeader {
    uint32_t magic = 0; // DKIF
    uint16_t version = 0;
    uint16_t header_size = 0;
    uint32_t fourcc = 0; // VP90
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t rate = 0;
    uint32_t scale = 0;
    uint32_t num_frames = 0;
    uint32_t unused = 0;
};

struct IvfFrame {
    uint32_t size = 0;
    uint64_t timestamp = 0;
    bool is_keyframe = false;
    std::span<const uint8_t> data;
    std::span<const uint8_t> record_bytes;
};

class IvfReader {
public:
    IvfReader() = default;

    std::expected<void, std::string> open(const std::filesystem::path& path);

    [[nodiscard]] const IvfHeader& get_header() const noexcept { return m_header; }
    [[nodiscard]] std::span<const uint8_t> get_raw_header() const noexcept { return m_raw_header; }
    [[nodiscard]] bool has_frames() const;
    std::expected<IvfFrame, std::string> read_next_frame();

private:
    io::reader m_reader;
    IvfHeader m_header;
    std::span<const uint8_t> m_raw_header;
    uint32_t m_current_frame = 0;
};

} // namespace cricodecs::video
