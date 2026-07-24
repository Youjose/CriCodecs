#pragma once

#include "document/document_types.hpp"

#include "awb_entry_codec.hpp"

#include <expected>
#include <span>
#include <stop_token>
#include <string>

namespace cristudio {

[[nodiscard]] bool is_ffmpeg_audio_codec(cricodecs::awb::EntryCodec codec);

[[nodiscard]] std::expected<AudioPreview, std::string> ffmpeg_audio_preview_from_bytes(
    cricodecs::awb::EntryCodec codec,
    std::span<const uint8_t> bytes,
    std::stop_token stop_token = {}
);

} // namespace cristudio
