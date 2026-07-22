#pragma once
/**
 * @file acb_container.hpp
 * @brief ACB (Atom Cue sheet Binary) - CRI cue sheet container
 *
 * Unified class for extraction and name resolution from ACB files.
 * An ACB is a UTF table containing sub-tables that map cue names to waveforms.
 * 
 * Name resolution chain (from vgmstream):
 *  CueName → Cue → [Synth|Sequence|BlockSequence] → ... → Waveform
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <flat_map>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <expected>
#include <optional>
#include "../utf/utf_table.hpp"
#include "../awb/awb_container.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::acb {

constexpr std::string_view encode_type_extension(uint8_t type) {
    switch (type) {
        case 0: case 3:  return ".adx";
        case 2: case 6:  return ".hca";
        case 7: case 10: return ".vag";
        case 8:          return ".at3";
        case 9:          return ".bcwav";
        case 11: case 18: return ".at9";
        case 12:         return ".xma";
        case 13: case 4: case 5: return ".dsp";
        case 19:         return ".m4a";
        case 24:         return ".opus";
        default:         return ".bin";
    }
}

struct WaveformInfo {
    uint16_t id = 0xFFFF;
    uint16_t memory_awb_id = 0xFFFF;
    uint16_t stream_awb_id = 0xFFFF;
    uint16_t port_no = 0xFFFF;
    uint8_t  streaming = 0;    // 0=memory, 1=streaming, 2=prefetch+stream
    uint8_t  encode_type = 0;
    bool     loop_flag = false;   // CRI LoopFlag: 2 = loop, 1 = no loop
    int32_t  extension_data = -1; // WaveformExtensionDataTable row, -1 when absent
};

struct WaveNameInfo {
    uint32_t waveform_index = 0;
    uint16_t wave_id = 0;
    uint16_t port_no = 0xFFFF;
    std::string name;
    std::string name_raw;
    uint8_t streaming = 0;
    uint8_t encode_type = 0;
};

struct WaveformAwbEntry {
    uint32_t waveform_index = 0;
    uint16_t wave_id = 0xFFFF;
    uint32_t awb_index = 0;
    bool stream_bank = false;
};

struct AcbSubtableInfo {
    std::string name;
    bool present = false;
    uint32_t row_count = 0;
    uint32_t column_count = 0;
    uint32_t data_size = 0;
};

class AcbContainer {
public:
    static constexpr int MAX_SYNTH_DEPTH = 3;
    static constexpr int MAX_SEQUENCE_DEPTH = 3;

    AcbContainer() = default;

    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        std::span<const uint8_t> data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        std::vector<uint8_t>&& data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        const std::filesystem::path& path,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] const std::vector<WaveNameInfo>& wave_names() const { return m_wave_names; }
    [[nodiscard]] std::string_view find_name(uint16_t wave_id) const;
    [[nodiscard]] std::string_view waveform_name(uint32_t index) const;
    [[nodiscard]] std::string_view waveform_name_raw(uint32_t index) const;
    [[nodiscard]] std::string waveform_filename(uint32_t index, bool include_index_prefix = false) const;
    [[nodiscard]] std::optional<std::span<const uint8_t>> embedded_awb() const;
    [[nodiscard]] bool has_embedded_awb() const;
    [[nodiscard]] std::optional<std::filesystem::path> companion_awb_path() const;
    [[nodiscard]] std::expected<awb::AwbContainer, std::string> load_awb() const;
    /// Resolve a waveform row through its AWB ID; row and AWB indices need not match.
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        const awb::AwbContainer& awb,
        bool prefer_stream_bank = false) const;
    /// Replace the resolved entry in the supplied editable bank; the ACB itself is unchanged.
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> replace_waveform_data(
        uint32_t index,
        awb::AwbContainer& awb,
        std::span<const uint8_t> data,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> replace_waveform_file(
        uint32_t index,
        awb::AwbContainer& awb,
        const std::filesystem::path& input_path,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<awb::AacEncryptionState, std::string> probe_waveform_aac_encryption(
        uint32_t index,
        uint64_t keycode) const;
    [[nodiscard]] bool has_aac_waveforms() const noexcept;
    [[nodiscard]] std::expected<awb::KeyRecoveryResult, std::string> recover_aac_key() const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_data(
        uint32_t index,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_stream_data(
        uint32_t index,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<void, std::string> extract(
        const std::filesystem::path& output_dir,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] uint32_t waveform_count() const { return static_cast<uint32_t>(m_waveforms.size()); }
    [[nodiscard]] const WaveformInfo& waveform(uint32_t index) const { return m_waveforms[index]; }
    [[nodiscard]] const utf::UtfTable& header_table() const noexcept { return m_header; }
    [[nodiscard]] std::vector<AcbSubtableInfo> subtable_info() const;
    [[nodiscard]] std::optional<std::reference_wrapper<const utf::UtfTable>> subtable(std::string_view name) const;
    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }

private:
    static constexpr int unresolved_column = -2;

    // Source data
    std::span<const uint8_t> m_source;
    std::vector<uint8_t> m_owned_source;
    std::filesystem::path m_source_path;
    text::EncodingOptions m_encoding;
    mutable std::optional<awb::AwbContainer> m_associated_awb;
    
    // Root "Header" table
    utf::UtfTable m_header;
    
    // Sub-tables (loaded lazily, cached)
    struct SubTables {
        std::optional<utf::UtfTable> cue_name_table;
        std::optional<utf::UtfTable> cue_table;
        std::optional<utf::UtfTable> synth_table;
        std::optional<utf::UtfTable> sequence_table;
        std::optional<utf::UtfTable> track_table;
        std::optional<utf::UtfTable> track_event_table;  // >= v1.28
        std::optional<utf::UtfTable> command_table;       // <= v1.27
        std::optional<utf::UtfTable> waveform_table;
        std::optional<utf::UtfTable> block_table;
        std::optional<utf::UtfTable> block_sequence_table;
        
        // Sub-table data must outlive the UtfTable that references it
        std::vector<std::vector<uint8_t>> table_data;

        struct CueColumns {
            int reference_type = unresolved_column;
            int reference_index = unresolved_column;
        } cue_columns;

        struct SynthColumns {
            int reference_items = unresolved_column;
        } synth_columns;

        struct SequenceColumns {
            int num_tracks = unresolved_column;
            int track_index = unresolved_column;
        } sequence_columns;

        struct TrackColumns {
            int event_index = unresolved_column;
        } track_columns;

        struct CommandColumns {
            int command = unresolved_column;
        } track_event_columns, command_columns;

        struct BlockColumns {
            int num_tracks = unresolved_column;
            int track_index = unresolved_column;
        } block_columns;

        struct BlockSequenceColumns {
            int num_tracks = unresolved_column;
            int track_index = unresolved_column;
            int num_blocks = unresolved_column;
            int block_index = unresolved_column;
        } block_sequence_columns;
    };

    struct SubtableDescriptor {
        const char* name = "";
        std::optional<utf::UtfTable> SubTables::* table = nullptr;
    };

    [[nodiscard]] static constexpr std::array<SubtableDescriptor, 10> subtable_descriptors() noexcept {
        return {{
            {"CueNameTable", &SubTables::cue_name_table},
            {"CueTable", &SubTables::cue_table},
            {"SynthTable", &SubTables::synth_table},
            {"SequenceTable", &SubTables::sequence_table},
            {"TrackTable", &SubTables::track_table},
            {"TrackEventTable", &SubTables::track_event_table},
            {"CommandTable", &SubTables::command_table},
            {"WaveformTable", &SubTables::waveform_table},
            {"BlockTable", &SubTables::block_table},
            {"BlockSequenceTable", &SubTables::block_sequence_table},
        }};
    }

    mutable SubTables m_sub;
    
    // Extracted info
    std::vector<WaveformInfo> m_waveforms;
    std::vector<WaveNameInfo> m_wave_names;
    std::vector<std::string> m_waveform_names;
    std::vector<std::string> m_waveform_names_raw;

    // Name lookup
    std::flat_map<uint16_t, std::string> m_name_map;

    std::optional<std::reference_wrapper<const utf::UtfTable>> load_subtable(const SubtableDescriptor& descriptor) const;
    std::optional<std::reference_wrapper<const utf::UtfTable>> load_subtable(std::string_view name) const;
    [[nodiscard]] std::expected<void, std::string> finish_load_from_source();
    bool preload_waveforms();
    bool preload_cue_names();

    struct ResolveContext {
        uint16_t target_wave_id = 0;
        int target_port = -1;
        bool is_memory = true;
        int synth_depth = 0;
        int sequence_depth = 0;
        std::string current_name;
        std::string current_name_raw;
        bool found = false;
    };

    struct CueNameRow {
        uint16_t cue_index = 0;
        std::string name;
        std::string name_raw;
    };

    ResolveContext m_resolve_ctx{};

    void resolve_all_names();
    void resolve_waveform_name(
        uint32_t waveform_index,
        bool is_memory_target,
        uint16_t wave_id,
        int target_port,
        std::span<const CueNameRow> cue_names);
    bool load_cue(uint16_t index);
    bool load_synth(uint16_t index);
    bool load_sequence(uint16_t index);
    bool load_track(uint16_t index);
    bool load_track_command(uint16_t index);
    bool load_command_tlvs(std::span<const uint8_t> data);
    bool load_waveform_check(uint16_t index);
    bool load_block(uint16_t index);
    bool load_block_sequence(uint16_t index);

    [[nodiscard]] static uint16_t waveform_id_for_bank(const WaveformInfo& waveform, bool is_memory_bank) noexcept;
    [[nodiscard]] static bool prefers_memory_bank(const WaveformInfo& waveform) noexcept;
    [[nodiscard]] static bool waveform_matches_bank(const WaveformInfo& waveform, bool is_memory_bank) noexcept;
    [[nodiscard]] bool uses_memory_bank_for_associated_awb(const WaveformInfo& waveform) const;
    [[nodiscard]] std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> associated_awb() const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> waveform_data_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_data_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        uint64_t aac_keycode,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<void, std::string> extract_file_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        const std::filesystem::path& output_path,
        uint64_t aac_keycode) const;
};

} // namespace cricodecs::acb
