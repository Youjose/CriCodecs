#pragma once
/**
 * @file awb_entry_codec.hpp
 * @brief Signature-based codec identification for AWB entry payloads.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cricodecs::awb {

enum class EntryCodec : uint8_t {
    Unknown,
    Hca,
    Adx,
    Ahx,
    AacM4a,
    AacAdts,
    OggVorbis,
    OggOpus,
    OggSpeex,
    Ogg,
    Wave,
    Flac,
    Mp3,
};

[[nodiscard]] constexpr bool has_bytes_at(
    std::span<const uint8_t> bytes,
    size_t offset,
    std::string_view expected) noexcept {
    return offset <= bytes.size() && expected.size() <= bytes.size() - offset &&
        std::equal(expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            [](char left, uint8_t right) { return static_cast<uint8_t>(left) == right; });
}

[[nodiscard]] constexpr EntryCodec probe_entry_codec(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() >= 4 &&
        (bytes[0] & 0x7Fu) == 'H' && (bytes[1] & 0x7Fu) == 'C' &&
        (bytes[2] & 0x7Fu) == 'A' && (bytes[3] & 0x7Fu) == 0) {
        return EntryCodec::Hca;
    }
    if (bytes.size() >= 5 && bytes[0] == 0x80 && bytes[1] == 0x00) {
        return bytes[4] == 0x10 || bytes[4] == 0x11 ? EntryCodec::Ahx : EntryCodec::Adx;
    }
    if (has_bytes_at(bytes, 4, "ftyp")) {
        return EntryCodec::AacM4a;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && (bytes[1] & 0xF6u) == 0xF0u) {
        return EntryCodec::AacAdts;
    }
    if (has_bytes_at(bytes, 0, "OggS")) {
        if (bytes.size() >= 27) {
            const auto segment_count = static_cast<size_t>(bytes[26]);
            const auto payload_offset = 27u + segment_count;
            if (payload_offset <= bytes.size()) {
                if (has_bytes_at(bytes, payload_offset, "OpusHead")) {
                    return EntryCodec::OggOpus;
                }
                if (payload_offset < bytes.size() && bytes[payload_offset] == 1 &&
                    has_bytes_at(bytes, payload_offset + 1, "vorbis")) {
                    return EntryCodec::OggVorbis;
                }
                if (has_bytes_at(bytes, payload_offset, "Speex   ")) {
                    return EntryCodec::OggSpeex;
                }
            }
        }
        return EntryCodec::Ogg;
    }
    if (has_bytes_at(bytes, 0, "RIFF") && has_bytes_at(bytes, 8, "WAVE")) {
        return EntryCodec::Wave;
    }
    if (has_bytes_at(bytes, 0, "fLaC")) {
        return EntryCodec::Flac;
    }
    if (has_bytes_at(bytes, 0, "ID3")) {
        return EntryCodec::Mp3;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xFF && (bytes[1] & 0xE0u) == 0xE0u) {
        const auto version = (bytes[1] >> 3) & 0x03u;
        const auto layer = (bytes[1] >> 1) & 0x03u;
        const auto bitrate = (bytes[2] >> 4) & 0x0Fu;
        const auto sample_rate = (bytes[2] >> 2) & 0x03u;
        if (version != 1 && layer != 0 && bitrate != 0 && bitrate != 0x0F && sample_rate != 3) {
            return EntryCodec::Mp3;
        }
    }
    return EntryCodec::Unknown;
}

[[nodiscard]] constexpr std::string_view entry_codec_name(EntryCodec codec) noexcept {
    switch (codec) {
    case EntryCodec::Hca: return "HCA audio";
    case EntryCodec::Adx: return "ADX audio";
    case EntryCodec::Ahx: return "AHX audio";
    case EntryCodec::AacM4a: return "AAC/M4A audio";
    case EntryCodec::AacAdts: return "AAC/ADTS audio";
    case EntryCodec::OggVorbis: return "Ogg/Vorbis audio";
    case EntryCodec::OggOpus: return "Ogg/Opus audio";
    case EntryCodec::OggSpeex: return "Ogg/Speex audio";
    case EntryCodec::Ogg: return "Ogg audio";
    case EntryCodec::Wave: return "WAV audio";
    case EntryCodec::Flac: return "FLAC audio";
    case EntryCodec::Mp3: return "MP3 audio";
    case EntryCodec::Unknown: return "audio";
    }
    return "audio";
}

[[nodiscard]] constexpr std::string_view entry_codec_extension(EntryCodec codec) noexcept {
    switch (codec) {
    case EntryCodec::AacM4a: return ".m4a";
    case EntryCodec::AacAdts: return ".aac";
    case EntryCodec::OggVorbis:
    case EntryCodec::Ogg: return ".ogg";
    case EntryCodec::OggOpus: return ".opus";
    case EntryCodec::OggSpeex: return ".spx";
    case EntryCodec::Wave: return ".wav";
    case EntryCodec::Flac: return ".flac";
    case EntryCodec::Mp3: return ".mp3";
    case EntryCodec::Hca: return ".hca";
    case EntryCodec::Adx: return ".adx";
    case EntryCodec::Ahx: return ".ahx";
    case EntryCodec::Unknown: return ".bin";
    }
    return ".bin";
}

} // namespace cricodecs::awb
