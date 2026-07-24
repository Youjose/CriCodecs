#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace cristudio {

struct DecryptionKeys {
    enum class AdxMode : uint8_t {
        None,
        Type8String,
        Type9Number,
        AhxTriplet
    };

    AdxMode adx_mode = AdxMode::None;
    std::string adx_type8_key;
    uint64_t adx_type9_key = 0;
    uint16_t adx_subkey = 0;
    uint16_t ahx_start = 0;
    uint16_t ahx_mult = 0;
    uint16_t ahx_add = 0;
    bool has_cri_key = false;
    uint64_t cri_key = 0;
    uint16_t hca_subkey = 0;
};

struct InfoRow {
    std::string name;
    std::string value;
};

struct EntrySummary {
    std::string name;
    std::string type;
    std::string size;
    std::string offset;
    std::string detail;
    std::vector<std::string> cells;
    std::vector<uint32_t> cell_source_indices;
    std::vector<EntrySummary> inspector_entries;
    std::vector<uint8_t> thumbnail_bytes;
    std::filesystem::path source_path;
    std::string source_format;
    uint32_t source_index = 0;
    bool has_source = false;
    bool has_cell_sources = false;
    std::string nested_source_format;
    uint32_t nested_source_index = 0;
    bool has_nested_source = false;
    uint32_t video_frame_rate_n = 0;
    uint32_t video_frame_rate_d = 0;
    uint32_t video_total_frames = 0;
    uint16_t hca_subkey = 0;
};

struct LoadedDocument {
    std::filesystem::path path;
    std::string display_name;
    std::string format;
    std::string loader_tag;
    uintmax_t file_size = 0;
    std::vector<InfoRow> info;
    std::vector<std::string> entry_columns;
    std::vector<std::string> entry_column_types;
    std::vector<EntrySummary> entries;
    bool summary_loaded = true;
};

struct AudioLoop {
    std::string name;
    uint64_t start_sample = 0;
    uint64_t end_sample = 0;
};

struct AudioPreview {
    std::filesystem::path playable_path;
    std::vector<uint8_t> wav_bytes;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint64_t sample_count = 0;
    std::string format;
    std::string note;
    std::vector<AudioLoop> loops;
};

struct VideoPreview {
    std::filesystem::path playable_path;
    std::filesystem::path temporary_directory;
    std::vector<uint8_t> video_bytes;
    std::string file_suffix;
    std::string ffmpeg_input_format;
    std::string format;
    std::string note;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
    uint64_t duration_ms = 0;
    bool remux_for_playback = false;
};

struct MuxAudioChoice {
    std::string name;
    std::string type;
    std::string detail;
    uint32_t source_index = 0;
};

struct MuxSubtitleChoice {
    std::string name;
    std::string detail;
    uint32_t source_index = 0;
    uint32_t language_id = 0;
    std::string srt_text;
};

struct MuxPreview {
    std::filesystem::path playable_path;
    std::filesystem::path temporary_directory;
    std::vector<uint8_t> video_bytes;
    std::string video_suffix;
    std::string ffmpeg_input_format;
    std::string format;
    std::string note;
    uint32_t frame_rate_n = 0;
    uint32_t frame_rate_d = 0;
    uint64_t duration_ms = 0;
    std::vector<MuxAudioChoice> audio_choices;
    int selected_audio = -1;
    std::vector<uint8_t> audio_wav_bytes;
    std::string audio_label;
    std::vector<MuxSubtitleChoice> subtitle_choices;
    int selected_subtitle = -1;
};

struct EmbeddedPreview {
    std::optional<LoadedDocument> document;
    std::optional<AudioPreview> audio;
    std::optional<VideoPreview> video;
    std::string hex_dump;
    std::string message;
    std::vector<uint8_t> raw_preview_bytes;
    uint64_t raw_total_size = 0;
    std::vector<uint8_t> preview_bytes;
    bool hex_truncated = false;
};

enum class ExtractionMode {
    Decoded,
    Raw
};

struct ExtractionEvent {
    size_t processed_delta = 0;
    size_t extracted_delta = 0;
    size_t failed_delta = 0;
    std::string message;
};

struct ExtractionOptions {
    bool include_mux_outputs = true;
    int mux_audio_choice = 0;
    std::filesystem::path ffmpeg_path;
    std::stop_token stop_token;
    std::function<void(const ExtractionEvent&)> event_callback;
};

struct ExtractionTarget {
    enum class Kind : uint8_t {
        Document,
        Entry
    };

    Kind kind = Kind::Document;
    LoadedDocument document;
    EntrySummary entry;
};

struct ExtractionReport {
    size_t total = 0;
    size_t extracted = 0;
    size_t failed = 0;
    bool canceled = false;
    bool messages_logged_live = false;
    std::optional<std::filesystem::path> diagnostic_path = std::nullopt;
    std::vector<std::filesystem::path> output_paths;
    std::vector<std::string> messages;
};
} // namespace cristudio
