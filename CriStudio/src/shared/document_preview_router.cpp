#include "shared/document_preview_router.hpp"

#include "modules/aax/aax_preview.hpp"
#include "modules/adx/adx_preview.hpp"
#include "modules/hca/hca_preview.hpp"
#include "modules/usm/usm_browse.hpp"
#include "modules/wav/wav_preview.hpp"
#include "shared/document_sniffer.hpp"
#include "shared/ffmpeg_audio_preview.hpp"
#include "shared/video_probe.hpp"

#include "awb_entry_codec.hpp"

#include <utility>
#include <array>
#include <ranges>
#include <vector>

namespace cristudio {
namespace {

bool has_editor_format_token(std::string_view text) {
    const auto lowered = lower_ascii(text);
    static constexpr std::array<std::string_view, 18> formats{
        "utf", "acb", "awb", "afs", "acx", "cpk", "cvm", "rofs", "csb",
        "adx", "ahx", "hca", "aax", "aix", "usm", "sfd", "sofdec", "sbt",
    };
    return std::ranges::any_of(formats, [&lowered](std::string_view format) {
        return lowered.find(format) != std::string::npos;
    });
}

bool is_standalone_media_text(std::string_view text) {
    const auto lowered = lower_ascii(text);
    static constexpr std::array<std::string_view, 9> formats{
        "mpeg", "h.264", "h264", "avc", "vp9", "ivf", "pcm", "wave", "wav",
    };
    return std::ranges::any_of(formats, [&lowered](std::string_view format) {
        return lowered.find(format) != std::string::npos;
    });
}

} // namespace

bool is_audio_format(std::string_view format) {
    const auto lowered = lower_ascii(format);
    return lowered.find("audio") != std::string::npos ||
           lowered.find("adx") != std::string::npos ||
           lowered.find("ahx") != std::string::npos ||
           lowered.find("aax") != std::string::npos ||
           lowered.find("hca") != std::string::npos ||
           lowered.find("wav") != std::string::npos;
}

bool is_direct_audio_format(std::string_view format) {
    const auto lowered = lower_ascii(format);
    return lowered.find("adx") != std::string::npos ||
           lowered.find("ahx") != std::string::npos ||
           lowered.find("aax") != std::string::npos ||
           lowered.find("hca") != std::string::npos ||
           lowered.find("wav") != std::string::npos ||
           lowered.find("aac") != std::string::npos ||
           lowered.find("m4a") != std::string::npos ||
           lowered.find("ogg") != std::string::npos ||
           lowered.find("vorbis") != std::string::npos ||
           lowered.find("opus") != std::string::npos ||
           lowered.find("speex") != std::string::npos ||
           lowered.find("flac") != std::string::npos ||
           lowered.find("mp3") != std::string::npos;
}

bool is_mux_document_format(std::string_view format) {
    const auto lowered = lower_ascii(format);
    return lowered.find("usm") != std::string::npos ||
           lowered.find("sfd") != std::string::npos ||
           lowered.find("sofdec") != std::string::npos;
}

bool is_audio_entry(const EntrySummary& entry) {
    const auto type = lower_ascii(entry.type);
    return type.find("audio") != std::string::npos ||
           type.find("adx") != std::string::npos ||
           type.find("ahx") != std::string::npos ||
           type.find("hca") != std::string::npos ||
           type.find("sfa") != std::string::npos ||
           type.find("wav") != std::string::npos;
}

bool is_sbt_entry(const EntrySummary& entry) {
    const auto text = lower_ascii(entry.type + " " + entry.source_format + " " + entry.nested_source_format);
    return text.find("sbt") != std::string::npos;
}

bool supports_editor(const LoadedDocument& document) {
    const auto semantic = document.loader_tag + " " + document.format;
    return !is_standalone_media_text(semantic) && has_editor_format_token(semantic);
}

bool supports_editor(const EntrySummary& entry) {
    const auto payload = entry.name + " " + entry.type + " " + entry.nested_source_format;
    if (is_standalone_media_text(payload)) {
        return false;
    }
    return has_editor_format_token(payload);
}

std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    const LoadedDocument& document,
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys,
    std::stop_token stop_token
) {
    const auto codec = cricodecs::awb::probe_entry_codec(bytes);
    using cricodecs::awb::EntryCodec;
    if (codec == EntryCodec::Adx || codec == EntryCodec::Ahx) {
        return modules::adx::audio_preview_from_bytes(bytes, keys);
    }
    if (codec == EntryCodec::Hca) {
        return modules::hca::audio_preview_from_bytes(bytes, keys);
    }
    if (codec == EntryCodec::Wave) {
        return modules::wav::audio_preview_from_bytes(bytes, document.path);
    }
    if (is_ffmpeg_audio_codec(codec)) {
        return ffmpeg_audio_preview_from_bytes(codec, bytes, stop_token);
    }

    const auto format = lower_ascii(document.format);
    if (format.find("adx") != std::string::npos || format.find("ahx") != std::string::npos) {
        return modules::adx::audio_preview_from_bytes(bytes, keys);
    }
    if (format.find("hca") != std::string::npos) {
        return modules::hca::audio_preview_from_bytes(bytes, keys);
    }
    if (format.find("aax") != std::string::npos) {
        return modules::aax::audio_preview_from_bytes(bytes, keys);
    }
    if (format.find("wav") != std::string::npos) {
        return modules::wav::audio_preview_from_bytes(bytes, document.path);
    }

    return std::unexpected("audio preview does not support this format yet");
}

std::optional<VideoPreview> video_preview_from_bytes(
    const EntrySummary& entry,
    std::span<const uint8_t> bytes
) {
    auto probe = probe_video_bytes(bytes);
    if (!probe) {
        return std::nullopt;
    }

    VideoPreview preview;
    preview.video_bytes.assign(bytes.begin(), bytes.end());
    preview.file_suffix = std::move(probe->suffix);
    preview.ffmpeg_input_format = std::move(probe->ffmpeg_input_format);
    preview.format = std::move(probe->format);
    preview.note = entry.name;
    preview.frame_rate_n = probe->frame_rate_n;
    preview.frame_rate_d = probe->frame_rate_d;
    preview.duration_ms = duration_from_frames(probe->frame_count, probe->frame_rate_n, probe->frame_rate_d);
    preview.remux_for_playback = probe->remux_for_playback;

    if (entry.video_frame_rate_n != 0 && entry.video_frame_rate_d != 0) {
        preview.frame_rate_n = entry.video_frame_rate_n;
        preview.frame_rate_d = entry.video_frame_rate_d;
    }
    if (entry.video_total_frames != 0) {
        preview.duration_ms = duration_from_frames(
            entry.video_total_frames,
            preview.frame_rate_n,
            preview.frame_rate_d
        );
    } else if (auto metadata = modules::usm::video_metadata_for_entry(entry)) {
        if (metadata->frame_rate_n != 0 && metadata->frame_rate_d != 0) {
            preview.frame_rate_n = metadata->frame_rate_n;
            preview.frame_rate_d = metadata->frame_rate_d;
        }
        if (metadata->total_frames != 0) {
            preview.duration_ms = duration_from_frames(
                metadata->total_frames,
                preview.frame_rate_n,
                preview.frame_rate_d
            );
        }
    }
    return preview;
}

std::expected<AudioPreview, std::string> build_direct_audio_preview(
    const LoadedDocument& document,
    const DecryptionKeys& keys
) {
    if (!is_direct_audio_format(document.format)) {
        return std::unexpected("selected document is not directly playable audio");
    }

    const auto format = lower_ascii(document.format);
    if (format.find("adx") != std::string::npos || format.find("ahx") != std::string::npos) {
        return modules::adx::audio_preview_from_file(document.path, keys);
    }
    if (format.find("hca") != std::string::npos) {
        return modules::hca::audio_preview_from_file(document.path, keys);
    }
    if (format.find("aax") != std::string::npos) {
        return modules::aax::audio_preview_from_file(document.path, keys);
    }
    if (format.find("wav") != std::string::npos) {
        return modules::wav::audio_preview_from_file(document.path);
    }

    return std::unexpected("audio preview does not support this format yet");
}

} // namespace cristudio
