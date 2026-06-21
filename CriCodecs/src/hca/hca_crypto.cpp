/**
 * @file hca_crypto.cpp
 * @brief HCA header and frame encryption/decryption helpers.
 *
 * Cipher behavior follows public HCA implementations and was cross-checked
 * against CRI SDK header masking/encryption behavior where recovered.
 */

#include "hca_crypto.hpp"
#include "hca_tables.hpp"

#include <array>

#include "../utilities/io.hpp"

namespace cricodecs::hca {

namespace {

using io::bit_reader;
using io::write_be;

void transform_frame_payload(const std::span<const uint8_t, 256> table, uint8_t* data, size_t size) noexcept {
    for (size_t i = 2; i + 2 < size; ++i) {
        data[i] = table[data[i]];
    }
}

consteval uint32_t masked_chunk_id(uint32_t clear_id) noexcept {
    const uint32_t zero_lanes = ((clear_id - 0x01010101u) & ~clear_id & 0x80808080u);
    return clear_id ^ (zero_lanes ^ 0x80808080u);
}

template <uint32_t ClearId>
void write_target_chunk_id(uint8_t* chunk_id, uint16_t target_cipher_type) noexcept {
    constexpr uint32_t masked_id = masked_chunk_id(ClearId);
    write_be<uint32_t>(chunk_id, target_cipher_type == 0 ? ClearId : masked_id);
}

void crypt_header(uint8_t* data, size_t header_size, uint16_t cipher_type) noexcept {
    if (header_size < 8) {
        return;
    }

    bit_reader reader(data, header_size);
    size_t remaining = header_size;

    if ((reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_HCA) {
        write_target_chunk_id<HCA_CHUNK_ID_HCA>(data + (reader.position() / 8), cipher_type);
        reader.skip(32 + 16 + 16);
        remaining -= 0x08;
    }

    if (remaining >= 0x10 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_FMT) {
        write_target_chunk_id<HCA_CHUNK_ID_FMT>(data + (reader.position() / 8), cipher_type);
        reader.skip(32 + 8 + 24 + 32 + 16 + 16);
        remaining -= 0x10;
    }

    if (remaining >= 0x10 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_COMP) {
        write_target_chunk_id<HCA_CHUNK_ID_COMP>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x10 * 8);
        remaining -= 0x10;
    } else if (remaining >= 0x0C && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_DEC) {
        write_target_chunk_id<HCA_CHUNK_ID_DEC>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x0C * 8);
        remaining -= 0x0C;
    }

    if (remaining >= 0x08 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_VBR) {
        write_target_chunk_id<HCA_CHUNK_ID_VBR>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x08 * 8);
        remaining -= 0x08;
    }

    if (remaining >= 0x06 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_ATH) {
        write_target_chunk_id<HCA_CHUNK_ID_ATH>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x06 * 8);
        remaining -= 0x06;
    }

    if (remaining >= 0x10 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_LOOP) {
        write_target_chunk_id<HCA_CHUNK_ID_LOOP>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x10 * 8);
        remaining -= 0x10;
    }

    if (remaining >= 0x06 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_CIPH) {
        const size_t offset = reader.position() / 8;
        write_target_chunk_id<HCA_CHUNK_ID_CIPH>(data + offset, cipher_type);
        write_be<uint16_t>(data + offset + 4, cipher_type);
        reader.skip(0x06 * 8);
        remaining -= 0x06;
    }

    if (remaining >= 0x08 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_RVA) {
        write_target_chunk_id<HCA_CHUNK_ID_RVA>(data + (reader.position() / 8), cipher_type);
        reader.skip(0x08 * 8);
        remaining -= 0x08;
    }

    if (remaining >= 0x05 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_COMM) {
        write_target_chunk_id<HCA_CHUNK_ID_COMM>(data + (reader.position() / 8), cipher_type);
        reader.skip(32);
        const uint8_t comment_len = static_cast<uint8_t>(reader.read(8));
        reader.skip(comment_len * 8);
        remaining = remaining >= static_cast<size_t>(0x05 + comment_len)
            ? remaining - static_cast<size_t>(0x05 + comment_len)
            : 0;
    }

    if (remaining >= 0x04 && (reader.peek(32) & HCA_MASK) == HCA_CHUNK_ID_PAD) {
        write_target_chunk_id<HCA_CHUNK_ID_PAD>(data + (reader.position() / 8), cipher_type);
    }

    write_be<uint16_t>(data + header_size - 2, tables::crc16_checksum(data, header_size - 2));
}

void transform_frames_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    std::span<const uint8_t, 256> table) noexcept {
    uint8_t* frame_data = hca_data.data() + info.file.header_size;
    for (uint32_t frame_index = 0; frame_index < info.fmt.frame_count; ++frame_index) {
        auto* frame = frame_data + frame_index * info.codec.frame_size;
        transform_frame_payload(table, frame, info.codec.frame_size);
        write_be<uint16_t>(frame + info.codec.frame_size - 2, tables::crc16_checksum(frame, info.codec.frame_size - 2));
    }
}

} // namespace

std::expected<void, std::string> detail::encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode) {
    if (cipher_type != 1 && cipher_type != 56) {
        return std::unexpected(std::string("HCA encrypt failed: unsupported target cipher type"));
    }

    std::array<uint8_t, 256> table{};
    cipher::init_cipher(table, cipher_type, keycode);

    std::array<uint8_t, 256> inverse{};
    for (size_t i = 0; i < inverse.size(); ++i) {
        inverse[table[i]] = static_cast<uint8_t>(i);
    }

    transform_frames_in_place(hca_data, info, inverse);
    crypt_header(hca_data.data(), info.file.header_size, cipher_type);
    return {};
}

std::expected<void, std::string> detail::encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode,
    uint16_t subkey) {
    return encrypt_in_place(hca_data, info, cipher_type, apply_subkey(keycode, subkey));
}

std::expected<void, std::string> detail::decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode) {
    std::array<uint8_t, 256> table{};
    cipher::init_cipher(table, info.cipher.type, keycode);

    transform_frames_in_place(hca_data, info, table);
    crypt_header(hca_data.data(), info.file.header_size, 0);
    return {};
}

std::expected<void, std::string> detail::decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode,
    uint16_t subkey) {
    return decrypt_in_place(hca_data, info, apply_subkey(keycode, subkey));
}

} // namespace cricodecs::hca
