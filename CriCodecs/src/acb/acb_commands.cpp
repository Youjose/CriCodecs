/**
 * @file acb_commands.cpp
 * @brief ACB command-stream parser implementation.
 *
 * Command coverage is based various sample scans, vgmstream/PyCriCodecsEx
 * research, and official lib authoring/runtime evidence. The current typed
 * dictionary and TLV scan verification are CriCodecs work by Youjose.
 */

#include "acb_commands.hpp"

#include "../utilities/io_endian.hpp"

#include <cstddef>

namespace cricodecs::acb {

std::string_view command_family_name(AcbCommandFamily family) noexcept {
    switch (family) {
        case AcbCommandFamily::terminator:        return "terminator";
        case AcbCommandFamily::target_reference:  return "target_reference";
        case AcbCommandFamily::timing:            return "timing";
        case AcbCommandFamily::runtime_parameter: return "runtime_parameter";
        case AcbCommandFamily::compact_runtime:   return "compact_runtime";
        case AcbCommandFamily::category:          return "category";
        case AcbCommandFamily::cue_limit:         return "cue_limit";
        case AcbCommandFamily::bus_send:          return "bus_send";
        case AcbCommandFamily::action:            return "action";
        case AcbCommandFamily::selector:          return "selector";
        case AcbCommandFamily::midi:              return "midi";
        case AcbCommandFamily::official_handled:  return "official_handled";
        case AcbCommandFamily::unknown:           return "unknown";
    }

    return "unknown";
}

std::string_view command_payload_kind_name(AcbCommandPayloadKind kind) noexcept {
    switch (kind) {
        case AcbCommandPayloadKind::none:                return "none";
        case AcbCommandPayloadKind::u8:                  return "u8";
        case AcbCommandPayloadKind::target_reference:    return "target_reference";
        case AcbCommandPayloadKind::be_u16:              return "be_u16";
        case AcbCommandPayloadKind::be_i16:              return "be_i16";
        case AcbCommandPayloadKind::be_u16_pair:         return "be_u16_pair";
        case AcbCommandPayloadKind::be_u16_pair_u8:      return "be_u16_pair_u8";
        case AcbCommandPayloadKind::be_f32:              return "be_f32";
        case AcbCommandPayloadKind::u8_pair:             return "u8_pair";
        case AcbCommandPayloadKind::parameter_curve_7:   return "parameter_curve_7";
        case AcbCommandPayloadKind::parameter_curve_11:  return "parameter_curve_11";
        case AcbCommandPayloadKind::category_id_list:    return "category_id_list";
        case AcbCommandPayloadKind::bus_name_send:       return "bus_name_send";
        case AcbCommandPayloadKind::sequence_wait_timer: return "sequence_wait_timer";
        case AcbCommandPayloadKind::raw:                 return "raw";
        case AcbCommandPayloadKind::variable:            return "variable";
    }

    return "raw";
}

std::expected<std::vector<AcbCommand>, std::string> parse_command_stream(std::span<const uint8_t> data) {
    std::vector<AcbCommand> commands;

    size_t pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 3) {
            return std::unexpected("ACB command stream has a truncated TLV header");
        }

        const uint16_t code = io::read_be<uint16_t>(data, pos);
        const uint8_t size = data[pos + 2];
        pos += 3;

        if (data.size() - pos < size) {
            return std::unexpected("ACB command stream payload exceeds command data size");
        }

        commands.push_back(AcbCommand{
            .code = code,
            .family = classify_command(code),
            .payload = data.subspan(pos, size),
        });

        pos += size;

        if (code == 0 && size == 0) {
            break;
        }
    }

    return commands;
}

std::optional<AcbCommandTarget> command_target_reference(const AcbCommand& command) noexcept {
    if (!is_waveform_reference_command(command.code) || command.payload.size() < 4) {
        return std::nullopt;
    }

    const uint16_t raw_type = io::read_be<uint16_t>(command.payload, 0);
    const auto type = static_cast<AcbCommandTargetType>(raw_type);
    switch (type) {
        case AcbCommandTargetType::waveform:
        case AcbCommandTargetType::synth:
        case AcbCommandTargetType::sequence:
            return AcbCommandTarget{
                .type = type,
                .index = io::read_be<uint16_t>(command.payload, 2),
            };
        case AcbCommandTargetType::none:
            return std::nullopt;
    }

    return std::nullopt;
}

} // namespace cricodecs::acb
