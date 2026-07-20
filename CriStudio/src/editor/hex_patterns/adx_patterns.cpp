#include "editor/hex_patterns/hex_patterns_internal.hpp"

#include "io_endian.hpp"

#include <algorithm>

namespace cristudio::hexpatterns {

void add_adx_patterns(std::vector<HexPatternRange>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 20 || cricodecs::io::read_be<uint16_t>(prefix.data()) != 0x8000u) {
        return;
    }
    const auto data_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint16_t>(prefix.data() + 2)) + 4u;
    const auto block_size = prefix[5];
    const auto channels = prefix[7];
    add(out, 0, data_offset, QStringLiteral("ADX header"), tone(0), total_size);
    add_field(out, 0, 2, QStringLiteral("ADX signature"), hex_value(cricodecs::io::read_be<uint16_t>(prefix.data()), 4), 0, total_size);
    add_field(out, 2, 2, QStringLiteral("data offset"), hex_value(data_offset), 1, total_size);
    add_field(out, 4, 1, QStringLiteral("encoding mode"), QString::number(prefix[4]), 2, total_size);
    add_field(out, 5, 1, QStringLiteral("block size"), QString::number(block_size), 3, total_size);
    add_field(out, 6, 1, QStringLiteral("bit depth"), QString::number(prefix[6]), 4, total_size);
    add_field(out, 7, 1, QStringLiteral("channels"), QString::number(channels), 5, total_size);
    add_field(out, 8, 4, QStringLiteral("sample rate"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 8)), 6, total_size);
    add_field(out, 12, 4, QStringLiteral("sample count"), QString::number(cricodecs::io::read_be<uint32_t>(prefix.data() + 12)), 7, total_size);
    add_field(out, 16, 2, QStringLiteral("highpass/cutoff freq"), QString::number(cricodecs::io::read_be<uint16_t>(prefix.data() + 16)), 8, total_size);
    add_field(out, 18, 1, QStringLiteral("version"), QString::number(prefix[18]), 8, total_size);
    add_field(out, 19, 1, QStringLiteral("flags"), hex_value(prefix[19], 2), 9, total_size);
    if (data_offset < total_size) {
        add(out, data_offset, total_size - data_offset, QStringLiteral("ADX frames"), tone(2), total_size);
    }
}

void add_adx_repeats(std::vector<HexPatternRepeat>& out, uint64_t total_size, std::span<const uint8_t> prefix) {
    if (prefix.size() < 20 || cricodecs::io::read_be<uint16_t>(prefix.data()) != 0x8000u) {
        return;
    }
    const auto data_offset = static_cast<uint64_t>(cricodecs::io::read_be<uint16_t>(prefix.data() + 2)) + 4u;
    const auto block_size = static_cast<uint64_t>(prefix[5]);
    const auto channels = std::max<uint8_t>(prefix[7], 1);
    if (block_size == 0 || data_offset >= total_size) {
        return;
    }
    const auto stride = block_size * channels;
    const auto blocks = (total_size - data_offset) / stride;
    add_repeat(out, data_offset, stride, stride, blocks, QStringLiteral("ADX frame"), tone(12), {}, total_size);
    for (uint8_t channel = 0; channel < channels; ++channel) {
        add_repeat(out, data_offset + static_cast<uint64_t>(channel) * block_size, stride, block_size, blocks,
            QStringLiteral("ADX channel %1 block").arg(channel), tone(13 + channel), {
                HexPatternRepeatField{.offset = 0, .size = 2, .name = QStringLiteral("scale"), .color = tone(20 + channel), .value_kind = HexPatternValueKind::U16BE},
                HexPatternRepeatField{.offset = 2, .size = block_size > 2 ? block_size - 2 : 0, .name = QStringLiteral("nibbles"), .color = tone(28 + channel), .value_kind = HexPatternValueKind::HexBytes}
            }, total_size);
    }
}

} // namespace cristudio::hexpatterns
