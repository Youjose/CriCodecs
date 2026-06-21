#pragma once

#include <vector>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace cricodecs::crilayla {

    /**
     * @brief Decompresses a full CRILAYLA blob.
     *
     * CRILAYLA stores a 0x100-byte uncompressed prefix after the payload and
     * compresses only the remaining body. The body bitstream is decoded
     * backwards: a 1-bit marker selects either an 8-bit literal or a
     * back-reference with a 13-bit offset and CRI's tiered variable-length
     * match-length coding.
     *
     * @param src Compressed data buffer, including the CRILAYLA header and
     *            trailing 0x100-byte prefix.
     * @return Full decompressed bytes, or a precise error for invalid or
     *         truncated input.
     */
    std::expected<std::vector<uint8_t>, std::string> decompress(std::span<const uint8_t> src);

    /**
     * @brief Compresses bytes into the native CRILAYLA stream used by CRI's tools.
     *
     * The official encoder keeps the first 0x100 bytes uncompressed, reverses
     * the remaining body, and then emits a forward bitstream that becomes a
     * backwards-decoded payload once the block is reversed into on-disk order.
     *
     * If the input is too small to benefit from the format, if the encoded body
     * would not be smaller than the raw body, or if the produced stream fails a
     * roundtrip verification, this function returns the original bytes instead
     * of a CRILAYLA wrapper. That matches how the official builder treats
     * those cases.
     *
     * @param src Raw bytes to compress.
     * @return CRILAYLA-wrapped bytes on success, or the original bytes when
     *         native-style compression should not be used.
     */
    std::vector<uint8_t> compress(std::span<const uint8_t> src);

} // namespace cricodecs::crilayla
