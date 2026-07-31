/**
 * @file acb_cue_graph.cpp
 * @brief Generic ACB cue graph parser and conservative command interpretation.
 */

#include "acb_cue_graph.hpp"

#include "../utf/utf_table.hpp"
#include "../utilities/io.hpp"
#include "../utilities/io_endian.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <utility>

namespace cricodecs::acb {

using utf::UtfTable;

namespace {

constexpr std::string_view parse_prefix = "ACB cue graph parse failed: ";

template <typename T>
std::optional<T> scalar(
    const UtfTable& table,
    uint32_t row,
    std::string_view name,
    std::vector<AcbCueDiagnostic>& diagnostics,
    std::string_view context
) {
    const int column = table.find_column(name);
    if (column < 0) {
        return std::nullopt;
    }
    auto value = table.get<T>(row, static_cast<uint32_t>(column));
    if (!value) {
        diagnostics.push_back({
            .context = std::string(context),
            .message = "column " + std::string(name) + ": " + value.error(),
        });
        return std::nullopt;
    }
    return *value;
}

std::vector<uint8_t> raw_data(
    const UtfTable& table,
    uint32_t row,
    std::string_view name,
    std::vector<AcbCueDiagnostic>& diagnostics,
    std::string_view context
) {
    const int column = table.find_column(name);
    if (column < 0) {
        return {};
    }
    auto data = table.get_data(row, static_cast<uint32_t>(column));
    if (!data) {
        diagnostics.push_back({
            .context = std::string(context),
            .message = "column " + std::string(name) + ": " + data.error(),
        });
        return {};
    }
    return {data->begin(), data->end()};
}

std::optional<uint32_t> unsigned32(
    const UtfTable& table,
    uint32_t row,
    std::string_view name,
    std::vector<AcbCueDiagnostic>& diagnostics,
    std::string_view context
) {
    const int column = table.find_column(name);
    if (column < 0) {
        return std::nullopt;
    }
    const auto column_index = static_cast<uint32_t>(column);
    switch (table.column(column_index).type) {
        case utf::ColumnType::UInt8:
            if (auto value = table.get<uint8_t>(row, column_index)) return *value;
            break;
        case utf::ColumnType::UInt16:
            if (auto value = table.get<uint16_t>(row, column_index)) return *value;
            break;
        case utf::ColumnType::UInt32:
            if (auto value = table.get<uint32_t>(row, column_index)) return *value;
            break;
        default:
            diagnostics.push_back({
                .context = std::string(context),
                .message = "column " + std::string(name) + " is not an unsigned integer",
            });
            return std::nullopt;
    }
    diagnostics.push_back({
        .context = std::string(context),
        .message = "column " + std::string(name) + " could not be read",
    });
    return std::nullopt;
}

std::vector<uint16_t> be_u16_values(std::span<const uint8_t> data) {
    std::vector<uint16_t> values;
    values.reserve(data.size() / 2);
    for (size_t offset = 0; offset + 2 <= data.size(); offset += 2) {
        values.push_back(io::read_be<uint16_t>(data, offset));
    }
    return values;
}

std::vector<uint16_t> counted_u16_values(
    std::span<const uint8_t> data,
    uint16_t count,
    std::vector<AcbCueDiagnostic>& diagnostics,
    std::string_view context
) {
    if (data.size() < static_cast<size_t>(count) * 2) {
        diagnostics.push_back({
            .context = std::string(context),
            .message = "u16 index list is shorter than its declared count",
        });
    }
    auto values = be_u16_values(data);
    if (values.size() > count) {
        values.resize(count);
    }
    return values;
}

std::expected<std::string, std::string> decode_string(
    std::string_view raw,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    auto decoded = text::decode_to_utf8(
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(raw.data()),
            raw.size()),
        encoding);
    if (!decoded) {
        return std::unexpected(
            std::string(parse_prefix) + std::string(context) + ": " + decoded.error());
    }
    return *decoded;
}

AcbCueCommand interpret_command(const AcbCommand& command) {
    AcbCueCommand result{
        .code = command.code,
        .dispatcher = command.dispatcher,
        .family = command.family,
        .meaning = AcbCueCommandMeaning::unknown,
        .evidence = AcbInterpretationEvidence::none,
        .payload = {command.payload.begin(), command.payload.end()},
        .target = std::nullopt,
        .argument_u16 = std::nullopt,
        .argument_u16_2 = std::nullopt,
        .argument_i16 = std::nullopt,
        .time_advance_us = std::nullopt,
    };

    if (const auto target = command_target_reference(command)) {
        result.meaning = AcbCueCommandMeaning::target_reference;
        result.evidence = AcbInterpretationEvidence::runtime_confirmed;
        result.target = *target;
        return result;
    }

    if (command.dispatcher == AcbCommandDispatcher::compact_parameter ||
        (command.dispatcher == AcbCommandDispatcher::legacy_shared &&
         command.code < 0x03E4)) {
        switch (command.code) {
            case 0:
                result.meaning = AcbCueCommandMeaning::terminator;
                result.evidence = AcbInterpretationEvidence::structural;
                break;
            case 33:
                result.meaning = AcbCueCommandMeaning::mute;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 65:
                result.meaning = AcbCueCommandMeaning::category_information;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 79:
                result.meaning = AcbCueCommandMeaning::cue_limit_information;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 72:
                // CriSolv adds this u8 to a playback-state counter. The
                // authoring/UI terminology for that counter is not known.
                result.meaning = AcbCueCommandMeaning::runtime_counter_add;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 81:
                // CriSolv writes this u8 to a runtime flag/output field. Keep
                // the deliberately structural name until its authoring label
                // is recovered.
                result.meaning = AcbCueCommandMeaning::runtime_flag_write;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 98:
                result.meaning = AcbCueCommandMeaning::sequence_wait_item;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 99:
                if (command.payload.size() == 2) {
                    // The runtime resolves this StringValue index as the
                    // selector name before applying selector conditions.
                    result.meaning = AcbCueCommandMeaning::selector_name;
                    result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                    result.argument_u16 = io::read_be<uint16_t>(command.payload, 0);
                }
                break;
            case 100:
                if (command.payload.size() == 4) {
                    // CriSolv's compact dispatcher resolves both big-endian
                    // u16 values through ACB string/value tables and stores
                    // the resulting selector condition.
                    result.meaning = AcbCueCommandMeaning::selector_condition;
                    result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                    result.argument_u16 = io::read_be<uint16_t>(command.payload, 0);
                    result.argument_u16_2 = io::read_be<uint16_t>(command.payload, 2);
                }
                break;
            case 111:
                result.meaning = AcbCueCommandMeaning::bus_send_by_name;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 120:
                result.meaning = AcbCueCommandMeaning::sequence_wait_timer;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            case 124:
                result.meaning = AcbCueCommandMeaning::stop_at_loop_end;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                break;
            default:
                break;
        }
        return result;
    }

    switch (command.code) {
        case 0:
            result.meaning = AcbCueCommandMeaning::terminator;
            result.evidence = AcbInterpretationEvidence::structural;
            break;
        case 2001:
            if (command.payload.size() == 4) {
                result.meaning = AcbCueCommandMeaning::wait_milliseconds;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                result.time_advance_us =
                    static_cast<uint64_t>(io::read_be<uint32_t>(command.payload, 0)) * 1000;
            }
            break;
        case 2005:
            if (command.payload.size() == 2) {
                // CriSolv 0x140063B6F sign-extends this big-endian value and
                // applies it to the same timing accumulator as command 2001.
                // Treating that runtime unit as microseconds is fixture-
                // inferred: it makes declared block lengths and every
                // observed Music.acb clip offset exact.
                result.meaning = AcbCueCommandMeaning::wait_submillisecond;
                result.evidence = AcbInterpretationEvidence::fixture_inferred;
                result.argument_i16 = io::read_be<int16_t>(command.payload, 0);
                result.time_advance_us = *result.argument_i16;
            }
            break;
        case 997:
            result.meaning = AcbCueCommandMeaning::sequence_start_milliseconds;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 998:
            result.meaning = AcbCueCommandMeaning::sequence_start_random;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 999:
            result.meaning = AcbCueCommandMeaning::sequence_start;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 4000:
            result.meaning = AcbCueCommandMeaning::midi_event;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 4050:
            // AtomCraft emits this at authored event end time; CriSolv uses
            // it to finalize the active timed track event.
            result.meaning = AcbCueCommandMeaning::end_track_event;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7099:
        case 7100:
            result.meaning = AcbCueCommandMeaning::start_action;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7101:
            result.meaning = AcbCueCommandMeaning::stop_action;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7102:
            result.meaning = AcbCueCommandMeaning::mute;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7104:
            if (command.payload.size() == 4) {
                // AtomCraft AcOoActionSetSelectorLabel serializer
                // (0x140A8E8A3) emits 7104 with this selector/label pair.
                result.meaning = AcbCueCommandMeaning::set_selector_label;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                result.argument_u16 = io::read_be<uint16_t>(command.payload, 0);
                result.argument_u16_2 = io::read_be<uint16_t>(command.payload, 2);
            }
            break;
        case 7107:
            // AtomCraft's AcOoActionPlaybackParam serializer emits 7107.
            // Preserve its version-dependent parameter/curve record raw.
            result.meaning = AcbCueCommandMeaning::playback_parameter;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7110:
            // AtomCraft emits 7101 and 7110 from AcOoActionStop variants.
            // CriSolv consumes 7110 as u16/u8/u8, but the submode names are
            // not established, so keep the payload raw.
            result.meaning = AcbCueCommandMeaning::stop_action_parameterized;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7108:
        case 7111:
            result.meaning = AcbCueCommandMeaning::pause_action;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7109:
        case 7112:
            result.meaning = AcbCueCommandMeaning::resume_action;
            result.evidence = AcbInterpretationEvidence::runtime_confirmed;
            break;
        case 7113:
            if (command.payload.size() == 2) {
                // AtomCraft's AcOoActionNextDestinationBlock serializer emits
                // 7113; CriSolv writes this value as the target playback's
                // next block index.
                result.meaning = AcbCueCommandMeaning::set_next_block;
                result.evidence = AcbInterpretationEvidence::runtime_confirmed;
                result.argument_u16 = io::read_be<uint16_t>(command.payload, 0);
            }
            break;
        case 7115:
            if (command.payload.size() == 2) {
                // This later target-addressed form is fixture-inferred from
                // named selector targets and matching StringValue rows. The
                // action concept is corroborated by AtomCraft's older 7104
                // AcOoActionSetSelectorLabel serializer.
                result.meaning = AcbCueCommandMeaning::set_selector_label;
                result.evidence = AcbInterpretationEvidence::fixture_inferred;
                result.argument_u16 = io::read_be<uint16_t>(command.payload, 0);
            }
            break;
        default:
            break;
    }

    return result;
}

std::expected<std::vector<AcbCueCommandStream>, std::string> parse_command_table(
    const UtfTable& table,
    AcbCommandTableKind kind
) {
    const int command_column = table.find_column("Command");
    if (command_column < 0) {
        return std::unexpected(
            std::string(parse_prefix) + std::string(table.table_name()) +
            " has no Command column");
    }

    const auto dispatcher = [kind] {
        switch (kind) {
            case AcbCommandTableKind::track_event:
                return AcbCommandDispatcher::serialized_event;
            case AcbCommandTableKind::legacy_command:
                return AcbCommandDispatcher::legacy_shared;
            case AcbCommandTableKind::sequence_command:
            case AcbCommandTableKind::track_command:
            case AcbCommandTableKind::synth_command:
                return AcbCommandDispatcher::compact_parameter;
        }
        return AcbCommandDispatcher::serialized_event;
    }();

    std::vector<AcbCueCommandStream> streams;
    streams.reserve(table.row_count());
    for (uint32_t row = 0; row < table.row_count(); ++row) {
        auto data = table.get_data(row, static_cast<uint32_t>(command_column));
        if (!data) {
            return std::unexpected(
                std::string(parse_prefix) + std::string(table.table_name()) +
                " row " + std::to_string(row) + ": " + data.error());
        }

        AcbCueCommandStream stream{
            .row_index = row,
            .table_kind = kind,
            .dispatcher = dispatcher,
            .raw = {data->begin(), data->end()},
            .commands = {},
            .scheduled_targets = {},
            .duration_us = 0,
            .uses_inferred_timing = false,
        };
        if (!data->empty()) {
            auto parsed = parse_command_stream(*data, dispatcher);
            if (!parsed) {
                return std::unexpected(
                    std::string(parse_prefix) + std::string(table.table_name()) +
                    " row " + std::to_string(row) + ": " + parsed.error());
            }

            stream.commands.reserve(parsed->size());
            int64_t clock_us = 0;
            for (uint32_t command_index = 0; command_index < parsed->size(); ++command_index) {
                auto interpreted = interpret_command((*parsed)[command_index]);
                if (interpreted.time_advance_us) {
                    clock_us += *interpreted.time_advance_us;
                    if (interpreted.meaning == AcbCueCommandMeaning::wait_submillisecond) {
                        stream.uses_inferred_timing = true;
                    }
                }
                if (interpreted.target) {
                    stream.scheduled_targets.push_back({
                        .command_index = command_index,
                        .time_us = clock_us,
                        .target = *interpreted.target,
                    });
                }
                stream.commands.push_back(std::move(interpreted));
            }
            stream.duration_us = clock_us;
        }
        streams.push_back(std::move(stream));
    }
    return streams;
}

} // namespace

class AcbCueGraphParser {
public:
    AcbCueGraphParser(AcbCueGraph& graph, const UtfTable& header, const text::EncodingOptions& encoding)
        : m_graph(graph), m_header(header), m_encoding(encoding) {}

    std::expected<void, std::string> parse() {
        if (m_header.table_name() != "Header" || m_header.row_count() != 1) {
            return std::unexpected(std::string(parse_prefix) + "root table is not a one-row Header");
        }

        if (const int awb_file = m_header.find_column("AwbFile"); awb_file >= 0) {
            auto data = m_header.get_data(0, static_cast<uint32_t>(awb_file));
            if (!data) {
                return std::unexpected(
                    std::string(parse_prefix) + "Header.AwbFile: " + data.error());
            }
            m_graph.m_has_embedded_awb = !data->empty();
        }

        if (auto result = parse_strings(); !result) return result;
        if (auto result = parse_outside_links(); !result) return result;
        if (auto result = parse_command_tables(); !result) return result;
        if (auto result = parse_waveforms(); !result) return result;
        if (auto result = parse_waveform_extensions(); !result) return result;
        if (auto result = parse_synths(); !result) return result;
        if (auto result = parse_tracks("TrackTable", m_graph.m_tracks); !result) return result;
        if (auto result = parse_tracks("ActionTrackTable", m_graph.m_action_tracks); !result) return result;
        if (auto result = parse_sequences(); !result) return result;
        if (auto result = parse_blocks(); !result) return result;
        if (auto result = parse_block_sequences(); !result) return result;
        if (auto result = parse_cues(); !result) return result;
        if (auto result = parse_cue_names(); !result) return result;
        link_cue_names();
        validate_references();
        return {};
    }

private:
    std::expected<std::optional<UtfTable>, std::string> table(std::string_view root_name) const {
        const int column = m_header.find_column(root_name);
        if (column < 0) {
            return std::optional<UtfTable>{};
        }
        auto data = m_header.get_data(0, static_cast<uint32_t>(column));
        if (!data) {
            return std::unexpected(
                std::string(parse_prefix) + std::string(root_name) + ": " + data.error());
        }
        if (data->empty()) {
            return std::optional<UtfTable>{};
        }
        auto parsed = UtfTable::load(*data);
        if (!parsed) {
            return std::unexpected(
                std::string(parse_prefix) + std::string(root_name) +
                " is not a valid nested UTF table: " + parsed.error());
        }
        return std::optional<UtfTable>{std::move(*parsed)};
    }

    template <typename T>
    T value_or(
        const UtfTable& table,
        uint32_t row,
        std::string_view name,
        T fallback,
        std::string_view context
    ) {
        return scalar<T>(table, row, name, m_graph.m_diagnostics, context).value_or(fallback);
    }

    std::expected<std::pair<std::string, std::string>, std::string> string_field(
        const UtfTable& table,
        uint32_t row,
        std::string_view name,
        std::string_view context
    ) const {
        const int column = table.find_column(name);
        if (column < 0) {
            return std::pair<std::string, std::string>{};
        }
        auto raw = table.get_string(row, static_cast<uint32_t>(column));
        if (!raw) {
            return std::unexpected(
                std::string(parse_prefix) + std::string(context) + ": " + raw.error());
        }
        auto decoded = decode_string(*raw, m_encoding, context);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        return std::pair{std::move(*decoded), std::string(*raw)};
    }

    std::expected<void, std::string> parse_strings() {
        auto result = table("StringValueTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& strings = **result;
        const int value_column = strings.find_column("StringValue");
        if (value_column < 0) {
            return std::unexpected(std::string(parse_prefix) + "StringValueTable has no StringValue column");
        }
        m_graph.m_strings.reserve(strings.row_count());
        for (uint32_t row = 0; row < strings.row_count(); ++row) {
            auto raw = strings.get_string(row, static_cast<uint32_t>(value_column));
            if (!raw) {
                return std::unexpected(
                    std::string(parse_prefix) + "StringValueTable row " +
                    std::to_string(row) + ": " + raw.error());
            }
            auto decoded = decode_string(*raw, m_encoding, "StringValueTable.StringValue");
            if (!decoded) return std::unexpected(decoded.error());
            m_graph.m_strings.push_back({
                .row_index = row,
                .value = std::move(*decoded),
                .value_raw = std::string(*raw),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_outside_links() {
        auto result = table("OutsideLinkTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& links = **result;
        m_graph.m_outside_links.reserve(links.row_count());
        for (uint32_t row = 0; row < links.row_count(); ++row) {
            const auto context = "OutsideLinkTable row " + std::to_string(row);
            m_graph.m_outside_links.push_back({
                .row_index = row,
                .cue_id = value_or<uint32_t>(links, row, "Id", 0xFFFFFFFFu, context),
                .cue_name_string_index = value_or<uint16_t>(
                    links, row, "StringIndex", invalid_acb_index, context),
                .acb_name_string_index = value_or<uint16_t>(
                    links, row, "AcbNameStringIndex", invalid_acb_index, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_one_command_table(
        std::string_view root_name,
        AcbCommandTableKind kind,
        std::vector<AcbCueCommandStream>& output
    ) {
        auto result = table(root_name);
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        auto streams = parse_command_table(**result, kind);
        if (!streams) return std::unexpected(streams.error());
        output = std::move(*streams);
        return {};
    }

    std::expected<void, std::string> parse_command_tables() {
        if (auto result = parse_one_command_table(
                "TrackEventTable", AcbCommandTableKind::track_event, m_graph.m_track_events);
            !result) return result;
        if (auto result = parse_one_command_table(
                "CommandTable", AcbCommandTableKind::legacy_command, m_graph.m_legacy_commands);
            !result) return result;
        if (auto result = parse_one_command_table(
                "SeqCommandTable", AcbCommandTableKind::sequence_command, m_graph.m_sequence_commands);
            !result) return result;
        if (auto result = parse_one_command_table(
                "TrackCommandTable", AcbCommandTableKind::track_command, m_graph.m_track_commands);
            !result) return result;
        return parse_one_command_table(
            "SynthCommandTable", AcbCommandTableKind::synth_command, m_graph.m_synth_commands);
    }

    std::expected<void, std::string> parse_waveforms() {
        auto result = table("WaveformTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) {
            return std::unexpected(std::string(parse_prefix) + "WaveformTable is missing");
        }
        const auto& table = **result;
        m_graph.m_waveforms.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "WaveformTable row " + std::to_string(row);
            const auto id = value_or<uint16_t>(
                table, row, "Id", invalid_acb_index, context);
            m_graph.m_waveforms.push_back({
                .row_index = row,
                .id = id,
                .memory_awb_id = value_or<uint16_t>(table, row, "MemoryAwbId",
                    id, context),
                .stream_awb_id = value_or<uint16_t>(table, row, "StreamAwbId", invalid_acb_index, context),
                .stream_awb_port_no = value_or<uint16_t>(table, row, "StreamAwbPortNo", invalid_acb_index, context),
                .streaming = value_or<uint8_t>(table, row, "Streaming", 0, context),
                .encode_type = value_or<uint8_t>(table, row, "EncodeType", 0, context),
                .num_channels = value_or<uint8_t>(table, row, "NumChannels", 0, context),
                .loop_flag = value_or<uint8_t>(table, row, "LoopFlag", 0, context),
                .sampling_rate = unsigned32(
                    table, row, "SamplingRate", m_graph.m_diagnostics, context).value_or(0),
                .num_samples = value_or<uint32_t>(table, row, "NumSamples", 0, context),
                .extension_data = value_or<uint16_t>(table, row, "ExtensionData", invalid_acb_index, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_waveform_extensions() {
        auto result = table("WaveformExtensionDataTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        m_graph.m_waveform_extensions.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "WaveformExtensionDataTable row " + std::to_string(row);
            m_graph.m_waveform_extensions.push_back({
                .row_index = row,
                .loop_start = value_or<uint32_t>(table, row, "LoopStart", 0, context),
                .loop_end = value_or<uint32_t>(table, row, "LoopEnd", 0, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_synths() {
        auto result = table("SynthTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        m_graph.m_synths.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "SynthTable row " + std::to_string(row);
            AcbSynth synth{
                .row_index = row,
                .type = value_or<uint8_t>(table, row, "Type", 0, context),
                .command_index = value_or<uint16_t>(table, row, "CommandIndex", invalid_acb_index, context),
                .action_track_start_index = value_or<uint16_t>(
                    table, row, "ActionTrackStartIndex", invalid_acb_index, context),
                .num_action_tracks = value_or<uint16_t>(table, row, "NumActionTracks", 0, context),
                .reference_items_raw = raw_data(
                    table, row, "ReferenceItems", m_graph.m_diagnostics, context),
                .reference_items = {},
                .track_values_raw = raw_data(
                    table, row, "TrackValues", m_graph.m_diagnostics, context),
                .track_values = {},
            };
            if (synth.reference_items_raw.size() % 4 != 0) {
                m_graph.m_diagnostics.push_back({
                    .context = context,
                    .message = "ReferenceItems has trailing bytes",
                });
            }
            for (size_t offset = 0; offset + 4 <= synth.reference_items_raw.size(); offset += 4) {
                synth.reference_items.push_back({
                    .type = io::read_be<uint16_t>(synth.reference_items_raw, offset),
                    .index = io::read_be<uint16_t>(synth.reference_items_raw, offset + 2),
                });
            }
            synth.track_values = be_u16_values(synth.track_values_raw);
            m_graph.m_synths.push_back(std::move(synth));
        }
        return {};
    }

    std::expected<void, std::string> parse_tracks(
        std::string_view root_name,
        std::vector<AcbTrack>& output
    ) {
        auto result = table(root_name);
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        output.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = std::string(root_name) + " row " + std::to_string(row);
            auto target_name = string_field(table, row, "TargetName", context);
            if (!target_name) return std::unexpected(target_name.error());
            auto target_acb = string_field(table, row, "TargetAcbName", context);
            if (!target_acb) return std::unexpected(target_acb.error());
            output.push_back({
                .row_index = row,
                .event_index = value_or<uint16_t>(table, row, "EventIndex", invalid_acb_index, context),
                .command_index = value_or<uint16_t>(table, row, "CommandIndex", invalid_acb_index, context),
                .target_type = value_or<uint8_t>(table, row, "TargetType", 0, context),
                .target_name = std::move(target_name->first),
                .target_name_raw = std::move(target_name->second),
                .target_id = value_or<uint32_t>(table, row, "TargetId", 0, context),
                .target_acb_name = std::move(target_acb->first),
                .target_acb_name_raw = std::move(target_acb->second),
                .scope = value_or<uint8_t>(table, row, "Scope", 0, context),
                .target_track_no = value_or<uint16_t>(table, row, "TargetTrackNo", invalid_acb_index, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_sequences() {
        auto result = table("SequenceTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        m_graph.m_sequences.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "SequenceTable row " + std::to_string(row);
            const auto track_count = value_or<uint16_t>(table, row, "NumTracks", 0, context);
            auto track_data = raw_data(table, row, "TrackIndex", m_graph.m_diagnostics, context);
            AcbSequence sequence{
                .row_index = row,
                .type = value_or<uint8_t>(table, row, "Type", 0, context),
                .playback_ratio = value_or<uint16_t>(table, row, "PlaybackRatio", 0, context),
                .command_index = value_or<uint16_t>(table, row, "CommandIndex", invalid_acb_index, context),
                .track_indices = counted_u16_values(
                    track_data, track_count, m_graph.m_diagnostics, context),
                .track_values_raw = raw_data(
                    table, row, "TrackValues", m_graph.m_diagnostics, context),
                .track_values = {},
                .action_track_start_index = value_or<uint16_t>(
                    table, row, "ActionTrackStartIndex", invalid_acb_index, context),
                .num_action_tracks = value_or<uint16_t>(table, row, "NumActionTracks", 0, context),
                .watch_action_start_index = value_or<uint16_t>(
                    table, row, "WatchActionStartIndex", invalid_acb_index, context),
                .num_watch_actions = value_or<uint16_t>(table, row, "NumWatchAction", 0, context),
                .stop_action_start_index = value_or<uint16_t>(
                    table, row, "StopActionStartIndex", invalid_acb_index, context),
                .num_stop_actions = value_or<uint16_t>(table, row, "NumStopAction", 0, context),
            };
            sequence.track_values = be_u16_values(sequence.track_values_raw);
            m_graph.m_sequences.push_back(std::move(sequence));
        }
        return {};
    }

    std::expected<void, std::string> parse_blocks() {
        auto result = table("BlockTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        m_graph.m_blocks.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "BlockTable row " + std::to_string(row);
            const auto track_count = value_or<uint16_t>(table, row, "NumTracks", 0, context);
            const auto destination_count =
                value_or<uint16_t>(table, row, "NumDestinationBlocks", 0, context);
            auto track_data = raw_data(table, row, "TrackIndex", m_graph.m_diagnostics, context);
            auto destination_data =
                raw_data(table, row, "DestinationBlocks", m_graph.m_diagnostics, context);
            m_graph.m_blocks.push_back({
                .row_index = row,
                .track_indices = counted_u16_values(
                    track_data, track_count, m_graph.m_diagnostics, context),
                .playback_type = value_or<uint8_t>(table, row, "PlaybackType", 0, context),
                .num_loops = value_or<uint16_t>(table, row, "NumLoops", 0, context),
                .transition_timing = value_or<uint8_t>(table, row, "TransitionTiming", 0, context),
                .transition_timing_value = value_or<uint16_t>(
                    table, row, "TransitionTimingValue", 0, context),
                .jump_previous_behavior = value_or<uint8_t>(
                    table, row, "JumpPreviousBehavior", 0, context),
                .jump_destination = value_or<uint8_t>(table, row, "JumpDestination", 0, context),
                .name_index = value_or<uint16_t>(table, row, "Name", invalid_acb_index, context),
                .length_ms = value_or<uint32_t>(table, row, "Length", 0, context),
                .length_submillisecond = value_or<uint16_t>(table, row, "LengthUs", 0, context),
                .start_position_ms = value_or<uint32_t>(table, row, "StartPosition", 0, context),
                .start_position_submillisecond = value_or<uint16_t>(
                    table, row, "StartPositionUs", 0, context),
                .action_track_start_index = value_or<uint16_t>(
                    table, row, "ActionTrackStartIndex", invalid_acb_index, context),
                .num_action_tracks = value_or<uint16_t>(table, row, "NumActionTracks", 0, context),
                .destination_blocks = counted_u16_values(
                    destination_data, destination_count, m_graph.m_diagnostics, context),
                .destination_values_raw = raw_data(
                    table, row, "DestinationBlockValues", m_graph.m_diagnostics, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_block_sequences() {
        auto result = table("BlockSequenceTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        m_graph.m_block_sequences.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "BlockSequenceTable row " + std::to_string(row);
            const auto track_count = value_or<uint16_t>(table, row, "NumTracks", 0, context);
            const auto block_count = value_or<uint16_t>(table, row, "NumBlocks", 0, context);
            auto track_data = raw_data(table, row, "TrackIndex", m_graph.m_diagnostics, context);
            auto block_data = raw_data(table, row, "BlockIndex", m_graph.m_diagnostics, context);
            AcbBlockSequence sequence{
                .row_index = row,
                .type = value_or<uint8_t>(table, row, "Type", 0, context),
                .playback_ratio = value_or<uint16_t>(table, row, "PlaybackRatio", 0, context),
                .command_index = value_or<uint16_t>(table, row, "CommandIndex", invalid_acb_index, context),
                .track_indices = counted_u16_values(
                    track_data, track_count, m_graph.m_diagnostics, context),
                .block_indices = counted_u16_values(
                    block_data, block_count, m_graph.m_diagnostics, context),
                .track_values_raw = raw_data(
                    table, row, "TrackValues", m_graph.m_diagnostics, context),
                .track_values = {},
                .watch_action_start_index = value_or<uint16_t>(
                    table, row, "WatchActionStartIndex", invalid_acb_index, context),
                .num_watch_actions = value_or<uint16_t>(table, row, "NumWatchAction", 0, context),
                .stop_action_start_index = value_or<uint16_t>(
                    table, row, "StopActionStartIndex", invalid_acb_index, context),
                .num_stop_actions = value_or<uint16_t>(table, row, "NumStopAction", 0, context),
            };
            sequence.track_values = be_u16_values(sequence.track_values_raw);
            m_graph.m_block_sequences.push_back(std::move(sequence));
        }
        return {};
    }

    std::expected<void, std::string> parse_cues() {
        auto result = table("CueTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) {
            return std::unexpected(std::string(parse_prefix) + "CueTable is missing");
        }
        const auto& table = **result;
        m_graph.m_cues.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            const auto context = "CueTable row " + std::to_string(row);
            m_graph.m_cues.push_back({
                .row_index = row,
                .cue_id = value_or<uint32_t>(table, row, "CueId", row, context),
                .reference = {
                    .type = value_or<uint8_t>(table, row, "ReferenceType", 0, context),
                    .index = value_or<uint16_t>(
                        table, row, "ReferenceIndex", invalid_acb_index, context),
                },
                .name_rows = {},
                .length = value_or<uint32_t>(table, row, "Length", 0, context),
                .worksize = value_or<uint16_t>(table, row, "Worksize", 0, context),
                .num_related_waveforms = value_or<uint16_t>(
                    table, row, "NumRelatedWaveforms", 0, context),
                .header_visibility = value_or<uint8_t>(
                    table, row, "HeaderVisibility", 0, context),
            });
        }
        return {};
    }

    std::expected<void, std::string> parse_cue_names() {
        auto result = table("CueNameTable");
        if (!result) return std::unexpected(result.error());
        if (!*result) return {};
        const auto& table = **result;
        const int name_column = table.find_column("CueName");
        const int index_column = table.find_column("CueIndex");
        if (name_column < 0 || index_column < 0) {
            return std::unexpected(
                std::string(parse_prefix) + "CueNameTable lacks CueName or CueIndex");
        }
        m_graph.m_cue_names.reserve(table.row_count());
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            auto raw = table.get_string(row, static_cast<uint32_t>(name_column));
            auto cue_index = table.get<uint16_t>(row, static_cast<uint32_t>(index_column));
            if (!raw || !cue_index) {
                return std::unexpected(
                    std::string(parse_prefix) + "CueNameTable row " + std::to_string(row) +
                    " is malformed");
            }
            auto decoded = decode_string(*raw, m_encoding, "CueNameTable.CueName");
            if (!decoded) return std::unexpected(decoded.error());
            m_graph.m_cue_names.push_back({
                .row_index = row,
                .cue_index = *cue_index,
                .name = std::move(*decoded),
                .name_raw = std::string(*raw),
            });
        }
        return {};
    }

    void link_cue_names() {
        for (const auto& name : m_graph.m_cue_names) {
            if (name.cue_index >= m_graph.m_cues.size()) {
                m_graph.m_diagnostics.push_back({
                    .context = "CueNameTable row " + std::to_string(name.row_index),
                    .message = "CueIndex is out of range",
                });
                continue;
            }
            m_graph.m_cues[name.cue_index].name_rows.push_back(name.row_index);
        }
    }

    void validate_references() {
        for (const auto& cue : m_graph.m_cues) {
            size_t limit = 0;
            switch (cue.reference.type) {
                case 0: continue;
                case 1: limit = m_graph.m_waveforms.size(); break;
                case 2: limit = m_graph.m_synths.size(); break;
                case 3: limit = m_graph.m_sequences.size(); break;
                case 5: limit = m_graph.m_outside_links.size(); break;
                case 6: limit = m_graph.m_synths.size(); break;
                case 7: limit = m_graph.m_sequences.size(); break;
                case 8: limit = m_graph.m_block_sequences.size(); break;
                case 9: limit = m_graph.m_block_sequences.size(); break;
                default:
                    m_graph.m_diagnostics.push_back({
                        .context = "CueTable row " + std::to_string(cue.row_index),
                        .message = "unknown ReferenceType " + std::to_string(cue.reference.type),
                    });
                    continue;
            }
            if (cue.reference.index >= limit) {
                m_graph.m_diagnostics.push_back({
                    .context = "CueTable row " + std::to_string(cue.row_index),
                    .message = "ReferenceIndex is out of range",
                });
            }
        }
    }

    AcbCueGraph& m_graph;
    const UtfTable& m_header;
    const text::EncodingOptions& m_encoding;
};

std::expected<AcbCueGraph, std::string> AcbCueGraph::load(
    std::span<const uint8_t> data,
    const text::EncodingOptions& encoding
) {
    auto header = UtfTable::load(data);
    if (!header) {
        return std::unexpected(std::string(parse_prefix) + "source is not a valid UTF table");
    }

    AcbCueGraph graph;
    AcbCueGraphParser parser(graph, *header, encoding);
    if (auto result = parser.parse(); !result) {
        return std::unexpected(result.error());
    }
    return graph;
}

std::expected<AcbCueGraph, std::string> AcbCueGraph::load(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    auto bytes = io::read_file_bytes(path, "ACB cue graph load failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return load(*bytes, encoding);
}

const AcbCue* AcbCueGraph::cue_by_id(uint32_t cue_id) const noexcept {
    const auto it = std::ranges::find(m_cues, cue_id, &AcbCue::cue_id);
    return it == m_cues.end() ? nullptr : &*it;
}

std::string_view AcbCueGraph::cue_name(uint32_t cue_index) const noexcept {
    if (cue_index >= m_cues.size() || m_cues[cue_index].name_rows.empty()) {
        return {};
    }
    const auto name_row = m_cues[cue_index].name_rows.front();
    return name_row < m_cue_names.size() ? m_cue_names[name_row].name : std::string_view{};
}

std::string_view AcbCueGraph::string_value(uint32_t string_index) const noexcept {
    return string_index < m_strings.size() ? m_strings[string_index].value : std::string_view{};
}

std::string_view AcbCueGraph::outside_link_cue_name(uint32_t link_index) const noexcept {
    return link_index < m_outside_links.size()
        ? string_value(m_outside_links[link_index].cue_name_string_index)
        : std::string_view{};
}

std::string_view AcbCueGraph::outside_link_acb_name(uint32_t link_index) const noexcept {
    return link_index < m_outside_links.size()
        ? string_value(m_outside_links[link_index].acb_name_string_index)
        : std::string_view{};
}

const AcbCueCommandStream* AcbCueGraph::command_stream(
    AcbCommandTableKind kind,
    uint32_t row_index
) const noexcept {
    const std::vector<AcbCueCommandStream>* streams = nullptr;
    switch (kind) {
        case AcbCommandTableKind::track_event:      streams = &m_track_events; break;
        case AcbCommandTableKind::legacy_command:   streams = &m_legacy_commands; break;
        case AcbCommandTableKind::sequence_command: streams = &m_sequence_commands; break;
        case AcbCommandTableKind::track_command:    streams = &m_track_commands; break;
        case AcbCommandTableKind::synth_command:    streams = &m_synth_commands; break;
    }
    return streams != nullptr && row_index < streams->size() ? &(*streams)[row_index] : nullptr;
}

std::expected<AcbCueAssembly, std::string> AcbCueGraph::assemble_cue(uint32_t cue_index) const {
    if (cue_index >= m_cues.size()) {
        return std::unexpected("ACB cue assembly failed: cue index is out of range");
    }

    AcbCueAssembly assembly{
        .cue_index = cue_index,
        .nodes = {},
        .edges = {},
        .unresolved = {},
        .has_cycle = false,
    };
    constexpr size_t node_kind_count =
        static_cast<size_t>(AcbCueNodeKind::outside_link) + 1;
    const std::array<size_t, node_kind_count> node_counts{
        m_cues.size(),
        m_waveforms.size(),
        m_synths.size(),
        m_sequences.size(),
        m_tracks.size(),
        m_action_tracks.size(),
        m_track_events.size(),
        m_legacy_commands.size(),
        m_sequence_commands.size(),
        m_track_commands.size(),
        m_synth_commands.size(),
        m_block_sequences.size(),
        m_blocks.size(),
        m_outside_links.size(),
    };
    std::array<size_t, node_kind_count> node_offsets{};
    size_t total_node_count = 0;
    for (size_t kind = 0; kind < node_kind_count; ++kind) {
        node_offsets[kind] = total_node_count;
        total_node_count += node_counts[kind];
    }
    // 0 = unseen, 1 = active in the current DFS path, 2 = complete.
    std::vector<uint8_t> node_state(total_node_count, 0);
    const auto node_slot = [&](AcbCueNode node) {
        return node_offsets[static_cast<size_t>(node.kind)] + node.index;
    };
    auto add_unresolved = [&](AcbCueNode source, uint16_t type, uint16_t index, std::string reason) {
        assembly.unresolved.push_back({
            .source = source,
            .type = type,
            .index = index,
            .reason = std::move(reason),
        });
    };

    std::function<void(AcbCueNode)> visit;
    auto connect = [&](AcbCueNode from, AcbCueNode to, AcbCueEdgeKind kind, uint32_t ordinal) {
        assembly.edges.push_back({from, to, kind, ordinal});
        visit(to);
    };

    visit = [&](AcbCueNode node) {
        auto& state = node_state[node_slot(node)];
        if (state == 1) {
            assembly.has_cycle = true;
            return;
        }
        if (state == 2) {
            return;
        }
        state = 1;
        assembly.nodes.push_back(node);

        auto connect_reference = [&](uint16_t type, uint16_t index, AcbCueEdgeKind edge_kind, uint32_t ordinal) {
            AcbCueNodeKind target_kind;
            size_t limit = 0;
            switch (type) {
                case 0:
                    return;
                case 1: target_kind = AcbCueNodeKind::waveform; limit = m_waveforms.size(); break;
                case 2: target_kind = AcbCueNodeKind::synth; limit = m_synths.size(); break;
                case 3: target_kind = AcbCueNodeKind::sequence; limit = m_sequences.size(); break;
                case 5: target_kind = AcbCueNodeKind::outside_link; limit = m_outside_links.size(); break;
                case 6: target_kind = AcbCueNodeKind::synth; limit = m_synths.size(); break;
                case 7: target_kind = AcbCueNodeKind::sequence; limit = m_sequences.size(); break;
                case 8: target_kind = AcbCueNodeKind::block_sequence; limit = m_block_sequences.size(); break;
                case 9: target_kind = AcbCueNodeKind::block_sequence; limit = m_block_sequences.size(); break;
                default:
                    add_unresolved(node, type, index, "unknown reference type");
                    return;
            }
            if (index >= limit) {
                add_unresolved(node, type, index, "reference index is out of range");
                return;
            }
            connect(
                node,
                {target_kind, index},
                type == 5 ? AcbCueEdgeKind::outside_link : edge_kind,
                ordinal);
        };

        auto connect_track = [&](uint16_t index, bool action, uint32_t ordinal) {
            const auto& rows = action ? m_action_tracks : m_tracks;
            if (index >= rows.size()) {
                add_unresolved(node, action ? 0xA001 : 0xA000, index, "track index is out of range");
                return;
            }
            connect(
                node,
                {action ? AcbCueNodeKind::action_track : AcbCueNodeKind::track, index},
                action ? AcbCueEdgeKind::action_track : AcbCueEdgeKind::track,
                ordinal);
        };

        auto connect_action_range = [&](uint16_t start, uint16_t count) {
            if (count == 0 || start == invalid_acb_index) return;
            for (uint32_t ordinal = 0; ordinal < count; ++ordinal) {
                const uint32_t index = static_cast<uint32_t>(start) + ordinal;
                if (index > std::numeric_limits<uint16_t>::max()) {
                    add_unresolved(node, 0xA001, invalid_acb_index, "action-track range overflows");
                    break;
                }
                connect_track(static_cast<uint16_t>(index), true, ordinal);
            }
        };

        auto connect_stream = [&](AcbCommandTableKind kind, uint16_t index, AcbCueEdgeKind edge_kind) {
            if (index == invalid_acb_index) return;
            const auto table_is_empty = [&] {
                switch (kind) {
                    case AcbCommandTableKind::track_event:      return m_track_events.empty();
                    case AcbCommandTableKind::legacy_command:   return m_legacy_commands.empty();
                    case AcbCommandTableKind::sequence_command: return m_sequence_commands.empty();
                    case AcbCommandTableKind::track_command:    return m_track_commands.empty();
                    case AcbCommandTableKind::synth_command:    return m_synth_commands.empty();
                }
                return true;
            }();
            // Older/smaller schemas can retain a numeric CommandIndex while
            // omitting the corresponding optional table entirely.
            if (table_is_empty) return;

            const auto stream_kind = [&]() -> std::optional<AcbCueNodeKind> {
                switch (kind) {
                    case AcbCommandTableKind::track_event:      return AcbCueNodeKind::track_event;
                    case AcbCommandTableKind::legacy_command:   return AcbCueNodeKind::legacy_command;
                    case AcbCommandTableKind::sequence_command: return AcbCueNodeKind::sequence_command;
                    case AcbCommandTableKind::track_command:    return AcbCueNodeKind::track_command;
                    case AcbCommandTableKind::synth_command:    return AcbCueNodeKind::synth_command;
                }
                return std::nullopt;
            }();
            if (!stream_kind) return;
            if (command_stream(kind, index) == nullptr) {
                add_unresolved(node, 0xC000 + static_cast<uint16_t>(kind), index,
                    "command-stream index is out of range");
                return;
            }
            connect(node, {*stream_kind, index}, edge_kind, 0);
        };

        switch (node.kind) {
            case AcbCueNodeKind::cue: {
                const auto& cue = m_cues[node.index];
                connect_reference(
                    cue.reference.type, cue.reference.index, AcbCueEdgeKind::cue_reference, 0);
                break;
            }
            case AcbCueNodeKind::synth: {
                const auto& synth = m_synths[node.index];
                for (uint32_t i = 0; i < synth.reference_items.size(); ++i) {
                    const auto& reference = synth.reference_items[i];
                    connect_reference(
                        reference.type, reference.index, AcbCueEdgeKind::synth_reference, i);
                }
                connect_action_range(synth.action_track_start_index, synth.num_action_tracks);
                connect_stream(
                    AcbCommandTableKind::synth_command,
                    synth.command_index,
                    AcbCueEdgeKind::parameter_stream);
                break;
            }
            case AcbCueNodeKind::sequence: {
                const auto& sequence = m_sequences[node.index];
                for (uint32_t i = 0; i < sequence.track_indices.size(); ++i) {
                    connect_track(sequence.track_indices[i], false, i);
                }
                connect_action_range(
                    sequence.action_track_start_index, sequence.num_action_tracks);
                connect_action_range(
                    sequence.watch_action_start_index, sequence.num_watch_actions);
                connect_action_range(
                    sequence.stop_action_start_index, sequence.num_stop_actions);
                connect_stream(
                    AcbCommandTableKind::sequence_command,
                    sequence.command_index,
                    AcbCueEdgeKind::parameter_stream);
                break;
            }
            case AcbCueNodeKind::track: {
                const auto& track = m_tracks[node.index];
                if (!m_track_events.empty()) {
                    connect_stream(
                        AcbCommandTableKind::track_event,
                        track.event_index,
                        AcbCueEdgeKind::event_stream);
                } else {
                    connect_stream(
                        AcbCommandTableKind::legacy_command,
                        track.event_index,
                        AcbCueEdgeKind::event_stream);
                }
                connect_stream(
                    AcbCommandTableKind::track_command,
                    track.command_index,
                    AcbCueEdgeKind::parameter_stream);
                break;
            }
            case AcbCueNodeKind::action_track: {
                const auto& track = m_action_tracks[node.index];
                // Unlike a normal TrackTable row, the action program is
                // selected by ActionTrackTable.CommandIndex. This relationship
                // holds across all action-bearing corpus schemas; EventIndex
                // is not the authored action program.
                if (!m_track_events.empty()) {
                    connect_stream(
                        AcbCommandTableKind::track_event,
                        track.command_index,
                        AcbCueEdgeKind::event_stream);
                } else {
                    connect_stream(
                        AcbCommandTableKind::legacy_command,
                        track.command_index,
                        AcbCueEdgeKind::event_stream);
                }
                break;
            }
            case AcbCueNodeKind::block_sequence: {
                const auto& sequence = m_block_sequences[node.index];
                for (uint32_t i = 0; i < sequence.track_indices.size(); ++i) {
                    connect_track(sequence.track_indices[i], false, i);
                }
                for (uint32_t i = 0; i < sequence.block_indices.size(); ++i) {
                    const auto block = sequence.block_indices[i];
                    if (block >= m_blocks.size()) {
                        add_unresolved(node, 8, block, "block index is out of range");
                    } else {
                        connect(node, {AcbCueNodeKind::block, block}, AcbCueEdgeKind::block, i);
                    }
                }
                connect_action_range(
                    sequence.watch_action_start_index, sequence.num_watch_actions);
                connect_action_range(
                    sequence.stop_action_start_index, sequence.num_stop_actions);
                connect_stream(
                    AcbCommandTableKind::sequence_command,
                    sequence.command_index,
                    AcbCueEdgeKind::parameter_stream);
                break;
            }
            case AcbCueNodeKind::block: {
                const auto& block = m_blocks[node.index];
                for (uint32_t i = 0; i < block.track_indices.size(); ++i) {
                    connect_track(block.track_indices[i], false, i);
                }
                connect_action_range(block.action_track_start_index, block.num_action_tracks);
                break;
            }
            case AcbCueNodeKind::track_event:
            case AcbCueNodeKind::legacy_command:
            case AcbCueNodeKind::sequence_command:
            case AcbCueNodeKind::track_command:
            case AcbCueNodeKind::synth_command: {
                AcbCommandTableKind kind;
                switch (node.kind) {
                    case AcbCueNodeKind::track_event:      kind = AcbCommandTableKind::track_event; break;
                    case AcbCueNodeKind::legacy_command:   kind = AcbCommandTableKind::legacy_command; break;
                    case AcbCueNodeKind::sequence_command: kind = AcbCommandTableKind::sequence_command; break;
                    case AcbCueNodeKind::track_command:    kind = AcbCommandTableKind::track_command; break;
                    case AcbCueNodeKind::synth_command:    kind = AcbCommandTableKind::synth_command; break;
                    default:                              kind = AcbCommandTableKind::track_event; break;
                }
                const auto* stream = command_stream(kind, node.index);
                if (stream != nullptr) {
                    uint32_t ordinal = 0;
                    for (const auto& command : stream->commands) {
                        if (!command.target) continue;
                        connect_reference(
                            static_cast<uint16_t>(command.target->type),
                            command.target->index,
                            AcbCueEdgeKind::command_target,
                            ordinal++);
                    }
                }
                break;
            }
            case AcbCueNodeKind::waveform:
                break;
            case AcbCueNodeKind::outside_link: {
                const auto& link = m_outside_links[node.index];
                auto reason = std::string("external ACB cue requires linked ACB");
                const auto acb_name = string_value(link.acb_name_string_index);
                const auto cue_name = string_value(link.cue_name_string_index);
                if (!acb_name.empty() || !cue_name.empty()) {
                    reason += ": ";
                    reason += acb_name.empty() ? "<current ACB>" : std::string(acb_name);
                    reason += '/';
                    reason += cue_name.empty() ? "<cue by ID>" : std::string(cue_name);
                }
                add_unresolved(node, 5, static_cast<uint16_t>(node.index), std::move(reason));
                break;
            }
        }

        state = 2;
    };

    visit({AcbCueNodeKind::cue, cue_index});
    return assembly;
}

} // namespace cricodecs::acb
