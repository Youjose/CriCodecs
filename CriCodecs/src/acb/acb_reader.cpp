/**
 * @file acb_reader.cpp
 * @brief ACB loading and cue-name resolution implementation
 *
 * Follows vgmstream's chain: CueName -> Cue -> Synth/Sequence/BlockSequence -> ... -> Waveform
 */

#include "acb_container.hpp"

#include "acb_commands.hpp"

#include <algorithm>
#include <initializer_list>
#include <concepts>
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::acb {

using utf::UtfTable;

namespace {

constexpr uint16_t INVALID_WAVE_ID = 0xFFFF;

[[nodiscard]] bool has_any_column(const UtfTable& table, std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (table.find_column(name) >= 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool looks_like_acb_header(const UtfTable& header) {
    if (header.table_name() != "Header" || header.row_count() != 1) {
        return false;
    }
    if (header.find_column("WaveformTable") < 0) {
        return false;
    }
    if (!has_any_column(header, {"CueNameTable", "CueTable", "AwbFile"})) {
        return false;
    }

    auto waveform_data = header.get_data(0, "WaveformTable");
    if (!waveform_data || waveform_data->empty()) {
        return false;
    }

    auto waveform_table = UtfTable::load(*waveform_data);
    if (!waveform_table || waveform_table->row_count() == 0) {
        return false;
    }

    const bool has_identity = has_any_column(*waveform_table, {"Id", "MemoryAwbId", "StreamAwbId"});
    const bool has_audio_shape = has_any_column(*waveform_table, {"EncodeType", "Streaming", "LoopFlag"});
    return has_identity && has_audio_shape;
}

template <std::invocable<uint16_t> Visit>
bool visit_u16_index_list(std::span<const uint8_t> data, uint16_t count_limit, Visit&& visit) {
    const uint16_t count = std::min(count_limit, static_cast<uint16_t>(data.size() / 2));
    for (uint16_t i = 0; i < count; ++i) {
        visit(io::read_be<uint16_t>(data, i * 2));
    }
    return true;
}

std::expected<std::string, std::string> decode_cri_string(
    std::string_view raw,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    auto decoded = text::decode_to_utf8(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()),
        encoding
    );
    if (!decoded) {
        return std::unexpected(std::string(context) + ": " + decoded.error());
    }
    return *decoded;
}


} // namespace

bool AcbContainer::load_subtable(const char* col_name, std::optional<UtfTable>& out) const {
    if (out.has_value()) {
        return true;
    }

    auto data_result = m_header.get_data(0, col_name);
    if (!data_result || data_result->empty()) {
        return false;
    }

    m_sub.table_data.emplace_back(data_result->begin(), data_result->end());
    auto& owned = m_sub.table_data.back();

    auto table = UtfTable::load(std::span<const uint8_t>(owned));
    if (!table) {
        m_sub.table_data.pop_back();
        return false;
    }

    out = std::move(*table);
    return true;
}

std::vector<AcbSubtableInfo> AcbContainer::subtable_info() const {
    auto make_info = [this](const char* name, std::optional<UtfTable>& table) {
        AcbSubtableInfo info;
        info.name = name;
        if (auto data = m_header.get_data(0, name); data && !data->empty()) {
            info.present = true;
            info.data_size = static_cast<uint32_t>(data->size());
            if (load_subtable(name, table) && table) {
                info.row_count = table->row_count();
                info.column_count = table->column_count();
            }
        }
        return info;
    };

    std::vector<AcbSubtableInfo> info;
    info.reserve(10);
    info.push_back(make_info("CueNameTable", m_sub.cue_name_table));
    info.push_back(make_info("CueTable", m_sub.cue_table));
    info.push_back(make_info("SynthTable", m_sub.synth_table));
    info.push_back(make_info("SequenceTable", m_sub.sequence_table));
    info.push_back(make_info("TrackTable", m_sub.track_table));
    info.push_back(make_info("TrackEventTable", m_sub.track_event_table));
    info.push_back(make_info("CommandTable", m_sub.command_table));
    info.push_back(make_info("WaveformTable", m_sub.waveform_table));
    info.push_back(make_info("BlockTable", m_sub.block_table));
    info.push_back(make_info("BlockSequenceTable", m_sub.block_sequence_table));
    return info;
}

std::optional<std::reference_wrapper<const UtfTable>> AcbContainer::subtable(std::string_view name) const {
    auto select = [this, name](std::string_view expected, const char* column_name, std::optional<UtfTable>& table)
        -> std::optional<std::reference_wrapper<const UtfTable>> {
        if (name != expected) {
            return std::nullopt;
        }
        if (!load_subtable(column_name, table) || !table) {
            return std::nullopt;
        }
        return std::cref(*table);
    };

    if (auto found = select("CueNameTable", "CueNameTable", m_sub.cue_name_table)) return found;
    if (auto found = select("CueTable", "CueTable", m_sub.cue_table)) return found;
    if (auto found = select("SynthTable", "SynthTable", m_sub.synth_table)) return found;
    if (auto found = select("SequenceTable", "SequenceTable", m_sub.sequence_table)) return found;
    if (auto found = select("TrackTable", "TrackTable", m_sub.track_table)) return found;
    if (auto found = select("TrackEventTable", "TrackEventTable", m_sub.track_event_table)) return found;
    if (auto found = select("CommandTable", "CommandTable", m_sub.command_table)) return found;
    if (auto found = select("WaveformTable", "WaveformTable", m_sub.waveform_table)) return found;
    if (auto found = select("BlockTable", "BlockTable", m_sub.block_table)) return found;
    if (auto found = select("BlockSequenceTable", "BlockSequenceTable", m_sub.block_sequence_table)) return found;
    return std::nullopt;
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    std::span<const uint8_t> data,
    const text::EncodingOptions& encoding
) {
    AcbContainer acb;
    acb.m_encoding = encoding;
    acb.m_owned_source.assign(data.begin(), data.end());
    acb.m_source_path.clear();
    acb.m_source = acb.m_owned_source;

    auto header = UtfTable::load(acb.m_source);
    if (!header) {
        return std::unexpected("ACB load failed: source is not a valid UTF table");
    }

    acb.m_header = std::move(*header);
    if (acb.m_header.row_count() == 0) {
        return std::unexpected("ACB load failed: header table has no rows");
    }
    if (!looks_like_acb_header(acb.m_header)) {
        return std::unexpected("ACB load failed: UTF table is not an ACB header");
    }
    acb.preload_waveforms();
    acb.preload_cue_names();
    acb.resolve_all_names();
    return acb;
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    std::vector<uint8_t>&& data,
    const text::EncodingOptions& encoding
) {
    AcbContainer acb;
    acb.m_encoding = encoding;
    acb.m_owned_source = std::move(data);
    acb.m_source_path.clear();
    acb.m_source = acb.m_owned_source;

    auto header = UtfTable::load(acb.m_source);
    if (!header) {
        return std::unexpected("ACB load failed: source is not a valid UTF table");
    }

    acb.m_header = std::move(*header);
    if (acb.m_header.row_count() == 0) {
        return std::unexpected("ACB load failed: header table has no rows");
    }
    if (!looks_like_acb_header(acb.m_header)) {
        return std::unexpected("ACB load failed: UTF table is not an ACB header");
    }
    acb.preload_waveforms();
    acb.preload_cue_names();
    acb.resolve_all_names();
    return acb;
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    auto bytes = io::read_file_bytes(path, "ACB load failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    AcbContainer acb;
    acb.m_encoding = encoding;
    acb.m_owned_source = std::move(*bytes);
    acb.m_source_path = path;
    acb.m_source = acb.m_owned_source;

    auto header = UtfTable::load(acb.m_source);
    if (!header) {
        return std::unexpected("ACB load failed: source is not a valid UTF table");
    }

    acb.m_header = std::move(*header);
    if (acb.m_header.row_count() == 0) {
        return std::unexpected("ACB load failed: header table has no rows");
    }
    if (!looks_like_acb_header(acb.m_header)) {
        return std::unexpected("ACB load failed: UTF table is not an ACB header");
    }
    acb.preload_waveforms();
    acb.preload_cue_names();
    acb.resolve_all_names();
    return acb;
}

bool AcbContainer::preload_waveforms() {
    if (!load_subtable("WaveformTable", m_sub.waveform_table)) {
        return false;
    }

    auto& wt = *m_sub.waveform_table;
    const uint32_t rows = wt.row_count();
    m_waveforms.resize(rows);
    m_waveform_names.assign(rows, {});
    m_waveform_names_raw.assign(rows, {});

    const int c_Id = wt.find_column("Id");
    const int c_MemoryAwbId = wt.find_column("MemoryAwbId");
    const int c_StreamAwbId = wt.find_column("StreamAwbId");
    const int c_StreamAwbPortNo = wt.find_column("StreamAwbPortNo");
    const int c_Streaming = wt.find_column("Streaming");
    const int c_EncodeType = wt.find_column("EncodeType");
    const int c_LoopFlag = wt.find_column("LoopFlag");
    const int c_ExtensionData = wt.find_column("ExtensionData");

    for (uint32_t i = 0; i < rows; ++i) {
        auto& waveform = m_waveforms[i];

        if (c_Streaming >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_Streaming))) {
                waveform.streaming = *value;
            }
        }

        if (c_Id >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_Id))) {
                waveform.id = *value;
                waveform.memory_awb_id = *value;
            }
        }

        if (c_MemoryAwbId >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_MemoryAwbId))) {
                waveform.memory_awb_id = *value;
            }
        }

        if (c_StreamAwbId >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_StreamAwbId))) {
                waveform.stream_awb_id = *value;
            }
        }

        if (c_StreamAwbPortNo >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_StreamAwbPortNo))) {
                waveform.port_no = *value;
            } else {
                waveform.port_no = 0;
            }
        }

        if (c_EncodeType >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_EncodeType))) {
                waveform.encode_type = *value;
            }
        }

        if (c_LoopFlag >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_LoopFlag))) {
                waveform.loop_flag = *value == 2;
            }
        }

        if (c_ExtensionData >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_ExtensionData))) {
                waveform.extension_data = *value == 0xFFFF
                    ? -1
                    : static_cast<int32_t>(*value);
            }
        }

        const bool is_memory_bank = waveform.streaming != 1;
        waveform.id = waveform_id_for_bank(waveform, is_memory_bank);
    }

    return true;
}

bool AcbContainer::preload_cue_names() {
    return load_subtable("CueNameTable", m_sub.cue_name_table);
}

void AcbContainer::resolve_waveform_name(uint32_t waveform_index,
                                         bool is_memory_target,
                                         uint16_t wave_id,
                                         int target_port,
                                         std::span<const CueNameRow> cue_names) {
    if (wave_id == INVALID_WAVE_ID || cue_names.empty()) {
        return;
    }

    m_resolve_ctx.target_wave_id = wave_id;
    m_resolve_ctx.target_port = target_port;
    m_resolve_ctx.is_memory = is_memory_target;

    for (const auto& cue_name : cue_names) {
        m_resolve_ctx.current_name_raw = cue_name.name_raw;
        m_resolve_ctx.current_name = cue_name.name;
        m_resolve_ctx.synth_depth = 0;
        m_resolve_ctx.sequence_depth = 0;
        m_resolve_ctx.found = false;

        load_cue(cue_name.cue_index);
        if (!m_resolve_ctx.found) {
            continue;
        }

        auto& resolved = m_waveform_names[waveform_index];
        auto& resolved_raw = m_waveform_names_raw[waveform_index];
        if (!resolved.empty() && resolved.find(m_resolve_ctx.current_name) == std::string::npos) {
            resolved += "; ";
            resolved += m_resolve_ctx.current_name;
            resolved_raw += "; ";
            resolved_raw += m_resolve_ctx.current_name_raw;
        } else if (resolved.empty()) {
            resolved = m_resolve_ctx.current_name;
            resolved_raw = m_resolve_ctx.current_name_raw;
        }
    }
}

void AcbContainer::resolve_all_names() {
    m_wave_names.clear();
    m_name_map.clear();
    if (!m_sub.cue_name_table) {
        return;
    }

    auto& cue_name_table = *m_sub.cue_name_table;
    const int c_CueName = cue_name_table.find_column("CueName");
    const int c_CueIndex = cue_name_table.find_column("CueIndex");
    if (c_CueName < 0 || c_CueIndex < 0) {
        return;
    }

    std::vector<CueNameRow> cue_names;
    cue_names.reserve(cue_name_table.row_count());
    for (uint32_t cue_row = 0; cue_row < cue_name_table.row_count(); ++cue_row) {
        auto cue_name = cue_name_table.get_string(cue_row, static_cast<uint32_t>(c_CueName));
        auto cue_index = cue_name_table.get<uint16_t>(cue_row, static_cast<uint32_t>(c_CueIndex));
        if (!cue_name || !cue_index) {
            continue;
        }

        auto decoded = decode_cri_string(*cue_name, m_encoding, "ACB CueName decode failed");
        if (!decoded) {
            continue;
        }

        cue_names.push_back(CueNameRow{
            .cue_index = *cue_index,
            .name = std::move(*decoded),
            .name_raw = std::string(*cue_name),
        });
    }

    for (uint32_t waveform_index = 0; waveform_index < m_waveforms.size(); ++waveform_index) {
        const auto& waveform = m_waveforms[waveform_index];
        const bool is_memory_target = waveform.streaming != 1;
        const uint16_t wave_id = waveform_id_for_bank(waveform, is_memory_target);
        const int target_port = is_memory_target || waveform.port_no == 0xFFFF
            ? -1
            : static_cast<int>(waveform.port_no);

        resolve_waveform_name(waveform_index, is_memory_target, wave_id, target_port, cue_names);

        auto resolved = waveform_name(waveform_index);
        if (resolved.empty()) {
            continue;
        }

        auto it = m_name_map.find(wave_id);
        if (it == m_name_map.end()) {
            m_name_map.emplace(wave_id, std::string(resolved));
        } else if (it->second.find(resolved) == std::string::npos) {
            it->second += "; ";
            it->second += resolved;
        }

        m_wave_names.push_back(WaveNameInfo{
            .waveform_index = waveform_index,
            .wave_id = wave_id,
            .port_no = waveform.port_no,
            .name = std::string(resolved),
            .name_raw = std::string(waveform_name_raw(waveform_index)),
            .streaming = waveform.streaming,
            .encode_type = waveform.encode_type,
        });
    }
}

bool AcbContainer::load_cue(uint16_t index) {
    if (!load_subtable("CueTable", m_sub.cue_table)) {
        return false;
    }
    auto& cue_table = *m_sub.cue_table;
    if (index >= cue_table.row_count()) {
        return false;
    }

    auto& columns = m_sub.cue_columns;
    if (columns.reference_type == unresolved_column) {
        columns.reference_type = cue_table.find_column("ReferenceType");
        columns.reference_index = cue_table.find_column("ReferenceIndex");
    }
    if (columns.reference_type < 0 || columns.reference_index < 0) {
        return false;
    }

    auto ref_type = cue_table.get<uint8_t>(index, static_cast<uint32_t>(columns.reference_type));
    auto ref_index = cue_table.get<uint16_t>(index, static_cast<uint32_t>(columns.reference_index));
    if (!ref_type || !ref_index) {
        return false;
    }

    switch (*ref_type) {
        case 1:
            return load_waveform_check(*ref_index);
        case 2:
            return load_synth(*ref_index);
        case 3:
            return load_sequence(*ref_index);
        case 8:
            return load_block_sequence(*ref_index);
        default:
            return true;
    }
}

bool AcbContainer::load_synth(uint16_t index) {
    if (!load_subtable("SynthTable", m_sub.synth_table)) {
        return false;
    }
    auto& synth_table = *m_sub.synth_table;
    if (index >= synth_table.row_count()) {
        return false;
    }

    m_resolve_ctx.synth_depth++;
    if (m_resolve_ctx.synth_depth > MAX_SYNTH_DEPTH) {
        m_resolve_ctx.synth_depth--;
        return false;
    }

    auto& columns = m_sub.synth_columns;
    if (columns.reference_items == unresolved_column) {
        columns.reference_items = synth_table.find_column("ReferenceItems");
    }
    if (columns.reference_items < 0) {
        m_resolve_ctx.synth_depth--;
        return false;
    }

    auto ri_data = synth_table.get_data(index, static_cast<uint32_t>(columns.reference_items));
    if (!ri_data || ri_data->empty()) {
        m_resolve_ctx.synth_depth--;
        return true;
    }

    auto items = *ri_data;
    const size_t count = items.size() / 4;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t item_type = io::read_be<uint16_t>(items, i * 4);
        const uint16_t item_index = io::read_be<uint16_t>(items, i * 4 + 2);

        switch (item_type) {
            case 0x00:
                i = count;
                break;
            case 0x01:
                load_waveform_check(item_index);
                break;
            case 0x02:
                load_synth(item_index);
                break;
            case 0x03:
                load_sequence(item_index);
                break;
            default:
                i = count;
                break;
        }
    }

    m_resolve_ctx.synth_depth--;
    return true;
}

bool AcbContainer::load_sequence(uint16_t index) {
    if (!load_subtable("SequenceTable", m_sub.sequence_table)) {
        return false;
    }
    auto& sequence_table = *m_sub.sequence_table;
    if (index >= sequence_table.row_count()) {
        return false;
    }

    m_resolve_ctx.sequence_depth++;
    if (m_resolve_ctx.sequence_depth > MAX_SEQUENCE_DEPTH) {
        m_resolve_ctx.sequence_depth--;
        return false;
    }

    auto& columns = m_sub.sequence_columns;
    if (columns.num_tracks == unresolved_column) {
        columns.num_tracks = sequence_table.find_column("NumTracks");
        columns.track_index = sequence_table.find_column("TrackIndex");
    }

    if (columns.num_tracks >= 0 && columns.track_index >= 0) {
        auto num_tracks = sequence_table.get<uint16_t>(index, static_cast<uint32_t>(columns.num_tracks));
        auto track_data = sequence_table.get_data(index, static_cast<uint32_t>(columns.track_index));
        if (num_tracks && track_data && !track_data->empty()) {
            visit_u16_index_list(*track_data, *num_tracks, [&](uint16_t track_index) {
                load_track(track_index);
            });
        }
    }

    m_resolve_ctx.sequence_depth--;
    return true;
}

bool AcbContainer::load_track(uint16_t index) {
    if (!load_subtable("TrackTable", m_sub.track_table)) {
        return false;
    }
    auto& track_table = *m_sub.track_table;
    if (index >= track_table.row_count()) {
        return false;
    }

    auto& columns = m_sub.track_columns;
    if (columns.event_index == unresolved_column) {
        columns.event_index = track_table.find_column("EventIndex");
    }
    if (columns.event_index < 0) {
        return true;
    }

    auto event_index = track_table.get<uint16_t>(index, static_cast<uint32_t>(columns.event_index));
    if (!event_index || *event_index == 0xFFFF) {
        return true;
    }

    return load_track_command(*event_index);
}

bool AcbContainer::load_track_command(uint16_t index) {
    if (!load_subtable("TrackEventTable", m_sub.track_event_table)) {
        if (!load_subtable("CommandTable", m_sub.command_table)) {
            return false;
        }
    }

    auto& table = m_sub.track_event_table ? *m_sub.track_event_table : *m_sub.command_table;
    auto& columns = m_sub.track_event_table ? m_sub.track_event_columns : m_sub.command_columns;
    if (index >= table.row_count()) {
        return false;
    }

    if (columns.command == unresolved_column) {
        columns.command = table.find_column("Command");
    }
    if (columns.command < 0) {
        return true;
    }

    auto cmd_data = table.get_data(index, static_cast<uint32_t>(columns.command));
    if (!cmd_data || cmd_data->empty()) {
        return true;
    }

    return load_command_tlvs(*cmd_data);
}

bool AcbContainer::load_command_tlvs(std::span<const uint8_t> data) {
    // The official runtime handles a broad command space. Name resolution only
    // needs target-reference commands, so all other parsed records are preserved
    // by acb_commands but ignored here.
    auto commands = parse_command_stream(data);
    if (!commands) {
        return false;
    }

    for (const AcbCommand& command : *commands) {
        const auto target = command_target_reference(command);
        if (!target) {
            continue;
        }

        switch (target->type) {
            case AcbCommandTargetType::synth:
                load_synth(target->index);
                break;
            case AcbCommandTargetType::sequence:
                load_sequence(target->index);
                break;
            case AcbCommandTargetType::waveform:
                load_waveform_check(target->index);
                break;
            case AcbCommandTargetType::none:
                break;
        }
    }

    return true;
}

bool AcbContainer::load_waveform_check(uint16_t index) {
    if (index >= m_waveforms.size()) {
        return false;
    }

    const auto& waveform = m_waveforms[index];
    if (!waveform_matches_bank(waveform, m_resolve_ctx.is_memory)) {
        return true;
    }

    const uint16_t waveform_id = waveform_id_for_bank(waveform, m_resolve_ctx.is_memory);
    if (waveform_id != m_resolve_ctx.target_wave_id) {
        return true;
    }

    if (m_resolve_ctx.target_port >= 0 && waveform.port_no != 0xFFFF && waveform.port_no != m_resolve_ctx.target_port) {
        return true;
    }

    m_resolve_ctx.found = true;
    return true;
}

bool AcbContainer::load_block(uint16_t index) {
    if (!load_subtable("BlockTable", m_sub.block_table)) {
        return false;
    }
    auto& block_table = *m_sub.block_table;
    if (index >= block_table.row_count()) {
        return false;
    }

    auto& columns = m_sub.block_columns;
    if (columns.num_tracks == unresolved_column) {
        columns.num_tracks = block_table.find_column("NumTracks");
        columns.track_index = block_table.find_column("TrackIndex");
    }

    if (columns.num_tracks >= 0 && columns.track_index >= 0) {
        auto num_tracks = block_table.get<uint16_t>(index, static_cast<uint32_t>(columns.num_tracks));
        auto track_data = block_table.get_data(index, static_cast<uint32_t>(columns.track_index));
        if (num_tracks && track_data && !track_data->empty()) {
            visit_u16_index_list(*track_data, *num_tracks, [&](uint16_t track_index) {
                load_track(track_index);
            });
        }
    }

    return true;
}

bool AcbContainer::load_block_sequence(uint16_t index) {
    if (!load_subtable("BlockSequenceTable", m_sub.block_sequence_table)) {
        return false;
    }
    auto& block_sequence_table = *m_sub.block_sequence_table;
    if (index >= block_sequence_table.row_count()) {
        return false;
    }

    auto& columns = m_sub.block_sequence_columns;
    if (columns.num_tracks == unresolved_column) {
        columns.num_tracks = block_sequence_table.find_column("NumTracks");
        columns.track_index = block_sequence_table.find_column("TrackIndex");
        columns.num_blocks = block_sequence_table.find_column("NumBlocks");
        columns.block_index = block_sequence_table.find_column("BlockIndex");
    }

    if (columns.num_tracks >= 0 && columns.track_index >= 0) {
        auto num_tracks = block_sequence_table.get<uint16_t>(index, static_cast<uint32_t>(columns.num_tracks));
        auto track_data = block_sequence_table.get_data(index, static_cast<uint32_t>(columns.track_index));
        if (num_tracks && track_data && !track_data->empty()) {
            visit_u16_index_list(*track_data, *num_tracks, [&](uint16_t track_index) {
                load_track(track_index);
            });
        }
    }

    if (columns.num_blocks >= 0 && columns.block_index >= 0) {
        auto num_blocks = block_sequence_table.get<uint16_t>(index, static_cast<uint32_t>(columns.num_blocks));
        auto block_data = block_sequence_table.get_data(index, static_cast<uint32_t>(columns.block_index));
        if (num_blocks && block_data && !block_data->empty()) {
            visit_u16_index_list(*block_data, *num_blocks, [&](uint16_t block_index) {
                load_block(block_index);
            });
        }
    }

    return true;
}

} // namespace cricodecs::acb
