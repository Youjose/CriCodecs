#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

namespace cristudio::hexpatterns {

void add_hca_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 8 || ((cricodecs::io::read_be<uint32_t>(prefix.data()) & 0x7F7F7F7Fu) != 0x48434100u)) {
        return;
    }
    const auto header_size = cricodecs::io::read_be<uint16_t>(prefix.data() + 6);
    add(out, 0, header_size == 0 ? 8 : header_size, QStringLiteral("HCA chunk header table"), tone(0), total_size);
    add_field(out, 0, 4, QStringLiteral("HCA magic"), ascii_value(prefix, 0, 4), 0, total_size);
    add_field(out, 4, 2, QStringLiteral("version"), hex_value(cricodecs::io::read_be<uint16_t>(prefix.data() + 4), 4), 1, total_size);
    add_field(out, 6, 2, QStringLiteral("header size"), QString::number(header_size), 2, total_size);

    size_t cursor = 8;
    int chunk_color = 3;
    while (cursor + 4 <= prefix.size() && cursor < header_size && chunk_color < 48) {
        const auto chunk_id = cricodecs::io::read_be<uint32_t>(prefix.data() + cursor) & 0x7F7F7F7Fu;
        if (chunk_id == 0x666D7400u && cursor + 0x10 <= prefix.size()) {
            add(out, cursor, 0x10, QStringLiteral("HCA fmt chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 0, 4, QStringLiteral("fmt.id"), ascii_value(prefix, cursor, 4), chunk_color++, total_size);
            add_field(out, cursor + 4, 1, QStringLiteral("fmt.channel_count"), QString::number(prefix[cursor + 4]), chunk_color++, total_size);
            const auto sample_rate = (static_cast<uint32_t>(prefix[cursor + 5]) << 16) |
                (static_cast<uint32_t>(prefix[cursor + 6]) << 8) |
                prefix[cursor + 7];
            const auto frame_count = cricodecs::io::read_be<uint32_t>(prefix.data() + cursor + 8);
            add_field(out, cursor + 5, 3, QStringLiteral("fmt.sample_rate"), QString::number(sample_rate), chunk_color++, total_size);
            add_field(out, cursor + 8, 4, QStringLiteral("fmt.frame_count"), QString::number(frame_count), chunk_color++, total_size);
            add_field(out, cursor + 12, 2, QStringLiteral("fmt.encoder_delay"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 12)), chunk_color++, total_size);
            add_field(out, cursor + 14, 2, QStringLiteral("fmt.encoder_padding"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 14)), chunk_color++, total_size);
            cursor += 0x10;
        } else if (chunk_id == 0x636F6D70u && cursor + 0x10 <= prefix.size()) {
            add(out, cursor, 0x10, QStringLiteral("HCA comp chunk"), tone(chunk_color++), total_size);
            const auto frame_size = cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4);
            add_field(out, cursor + 4, 2, QStringLiteral("comp.frame_size"), QString::number(frame_size), chunk_color++, total_size);
            add_field(out, cursor + 6, 1, QStringLiteral("comp.min_resolution"), QString::number(prefix[cursor + 6]), chunk_color++, total_size);
            add_field(out, cursor + 7, 1, QStringLiteral("comp.max_resolution"), QString::number(prefix[cursor + 7]), chunk_color++, total_size);
            add_field(out, cursor + 8, 1, QStringLiteral("comp.track_count"), QString::number(prefix[cursor + 8]), chunk_color++, total_size);
            add_field(out, cursor + 9, 1, QStringLiteral("comp.channel_config"), QString::number(prefix[cursor + 9]), chunk_color++, total_size);
            add_field(out, cursor + 10, 1, QStringLiteral("comp.total_band_count"), QString::number(prefix[cursor + 10]), chunk_color++, total_size);
            add_field(out, cursor + 11, 1, QStringLiteral("comp.base_band_count"), QString::number(prefix[cursor + 11]), chunk_color++, total_size);
            add_field(out, cursor + 12, 1, QStringLiteral("comp.stereo_band_count"), QString::number(prefix[cursor + 12]), chunk_color++, total_size);
            add_field(out, cursor + 13, 1, QStringLiteral("comp.bands_per_hfr_group"), QString::number(prefix[cursor + 13]), chunk_color++, total_size);
            add_field(out, cursor + 14, 1, QStringLiteral("comp.reserved"), hex_value(prefix[cursor + 14], 2), chunk_color++, total_size);
            add_field(out, cursor + 15, 1, QStringLiteral("comp.extension"), hex_value(prefix[cursor + 15], 2), chunk_color++, total_size);
            cursor += 0x10;
        } else if (chunk_id == 0x64656300u && cursor + 0x0C <= prefix.size()) {
            add(out, cursor, 0x0C, QStringLiteral("HCA dec chunk"), tone(chunk_color++), total_size);
            const auto frame_size = cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4);
            add_field(out, cursor + 4, 2, QStringLiteral("dec.frame_size"), QString::number(frame_size), chunk_color++, total_size);
            add_field(out, cursor + 6, 1, QStringLiteral("dec.min_resolution"), QString::number(prefix[cursor + 6]), chunk_color++, total_size);
            add_field(out, cursor + 7, 1, QStringLiteral("dec.max_resolution"), QString::number(prefix[cursor + 7]), chunk_color++, total_size);
            add_field(out, cursor + 8, 1, QStringLiteral("dec.total_band_count"), QString::number(static_cast<int>(prefix[cursor + 8]) + 1), chunk_color++, total_size);
            add_field(out, cursor + 9, 1, QStringLiteral("dec.base_band_count"), QString::number(static_cast<int>(prefix[cursor + 9]) + 1), chunk_color++, total_size);
            add_field(out, cursor + 10, 1, QStringLiteral("dec.track_config"), hex_value(prefix[cursor + 10], 2), chunk_color++, total_size);
            add_field(out, cursor + 11, 1, QStringLiteral("dec.stereo_type"), QString::number(prefix[cursor + 11]), chunk_color++, total_size);
            cursor += 0x0C;
        } else if (chunk_id == 0x61746800u && cursor + 0x06 <= prefix.size()) {
            add(out, cursor, 0x06, QStringLiteral("HCA ath chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 4, 2, QStringLiteral("ath.type"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4)), chunk_color++, total_size);
            cursor += 0x06;
        } else if (chunk_id == 0x63697068u && cursor + 0x06 <= prefix.size()) {
            add(out, cursor, 0x06, QStringLiteral("HCA ciph chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 4, 2, QStringLiteral("ciph.type"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4)), chunk_color++, total_size);
            cursor += 0x06;
        } else if (chunk_id == 0x6C6F6F70u && cursor + 0x10 <= prefix.size()) {
            add(out, cursor, 0x10, QStringLiteral("HCA loop chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 4, 4, QStringLiteral("loop.start_frame"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + cursor + 4)), chunk_color++, total_size);
            add_field(out, cursor + 8, 4, QStringLiteral("loop.end_frame"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + cursor + 8)), chunk_color++, total_size);
            add_field(out, cursor + 12, 2, QStringLiteral("loop.start_delay"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 12)), chunk_color++, total_size);
            add_field(out, cursor + 14, 2, QStringLiteral("loop.end_padding"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 14)), chunk_color++, total_size);
            cursor += 0x10;
        } else if (chunk_id == 0x76627200u && cursor + 0x08 <= prefix.size()) {
            add(out, cursor, 0x08, QStringLiteral("HCA vbr chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 4, 2, QStringLiteral("vbr.max_frame_size"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4)), chunk_color++, total_size);
            add_field(out, cursor + 6, 2, QStringLiteral("vbr.noise_level"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 6)), chunk_color++, total_size);
            cursor += 0x08;
        } else if (chunk_id == 0x72766100u && cursor + 0x08 <= prefix.size()) {
            add(out, cursor, 0x08, QStringLiteral("HCA rva chunk"), tone(chunk_color++), total_size);
            add_field(out, cursor + 4, 4, QStringLiteral("rva.volume"), hex_value(cricodecs::io::read_be<uint32_t>(prefix.data() + cursor + 4), 8), chunk_color++, total_size);
            cursor += 0x08;
        } else {
            break;
        }
    }

    if (header_size < total_size) {
        add(out, header_size, total_size - header_size, QStringLiteral("HCA frames"), tone(2), total_size);
    }
}

void add_hca_repeats(std::vector<HexPatternRepeat>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 8 || ((cricodecs::io::read_be<uint32_t>(prefix.data()) & 0x7F7F7F7Fu) != 0x48434100u)) {
        return;
    }
    const auto header_size = cricodecs::io::read_be<uint16_t>(prefix.data() + 6);
    if (header_size == 0 || header_size >= total_size) {
        return;
    }
    uint32_t frame_count = 0;
    uint16_t frame_size = 0;
    size_t cursor = 8;
    while (cursor + 4 <= prefix.size() && cursor < header_size) {
        const auto chunk_id = cricodecs::io::read_be<uint32_t>(prefix.data() + cursor) & 0x7F7F7F7Fu;
        if (chunk_id == 0x666D7400u && cursor + 0x10 <= prefix.size()) {
            frame_count = cricodecs::io::read_be<uint32_t>(prefix.data() + cursor + 8);
            cursor += 0x10;
        } else if (chunk_id == 0x636F6D70u && cursor + 0x10 <= prefix.size()) {
            frame_size = cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4);
            cursor += 0x10;
        } else if (chunk_id == 0x64656300u && cursor + 0x0C <= prefix.size()) {
            frame_size = cricodecs::io::read_be<uint16_t>(prefix.data() + cursor + 4);
            cursor += 0x0C;
        } else if (chunk_id == 0x61746800u && cursor + 0x06 <= prefix.size()) {
            cursor += 0x06;
        } else if (chunk_id == 0x63697068u && cursor + 0x06 <= prefix.size()) {
            cursor += 0x06;
        } else if (chunk_id == 0x6C6F6F70u && cursor + 0x10 <= prefix.size()) {
            cursor += 0x10;
        } else if (chunk_id == 0x76627200u && cursor + 0x08 <= prefix.size()) {
            cursor += 0x08;
        } else if (chunk_id == 0x72766100u && cursor + 0x08 <= prefix.size()) {
            cursor += 0x08;
        } else {
            break;
        }
    }
    if (frame_size == 0) {
        return;
    }
    const auto count = frame_count == 0 ? (total_size - header_size) / frame_size : frame_count;
    add_repeat(out, header_size, frame_size, frame_size, count, QStringLiteral("HCA frame"), tone(12), {
        HexPatternRepeatField{.offset = 0, .size = 2, .name = QStringLiteral("sync"), .color = tone(13), .value_kind = HexPatternValueKind::U16BE},
        HexPatternRepeatField{.offset = 2, .size = 2, .name = QStringLiteral("noise/evaluation bits"), .color = tone(14), .value_kind = HexPatternValueKind::U16BE},
        HexPatternRepeatField{.offset = 4, .size = frame_size > 6 ? static_cast<uint64_t>(frame_size - 6) : 0u, .name = QStringLiteral("packed scalefactors/spectra"), .color = tone(15), .value_kind = HexPatternValueKind::HexBytes},
        HexPatternRepeatField{.offset = frame_size > 1 ? static_cast<uint64_t>(frame_size - 2) : 0u, .size = 2, .name = QStringLiteral("frame checksum"), .color = tone(16), .value_kind = HexPatternValueKind::U16BE}
    }, total_size);
}

} // namespace cristudio::hexpatterns
