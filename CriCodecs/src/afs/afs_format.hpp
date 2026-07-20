#pragma once
/**
 * @file afs_format.hpp
 * @brief Internal classic AFS constants and serialized field helpers.
 *
 * Constants follow the reviewed `AFS\0` layout from CRI AfsLink/afslnk output.
 * Helper extraction and validation code is CriCodecs work by Youjose.
 */

#include "afs_container.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "../utilities/io.hpp"

namespace cricodecs::afs::detail {

using io::read_be;
using io::read_le;

constexpr io::FourCC afs_magic{"AFS\0"};
constexpr uint32_t adx_signature_mask = 0xFFFF0000u;
constexpr uint32_t adx_signature_value = 0x80000000u;
constexpr uint32_t ogg_magic = io::FourCC{"OggS"}.be_value();
constexpr uint32_t hca_magic = io::FourCC{"HCA\0"}.be_value();
constexpr size_t directory_entry_size = 0x30;
constexpr size_t directory_name_size = 0x20;

[[nodiscard]] inline AfsEntryType detect_entry_type(std::span<const uint8_t> source, uint32_t offset, uint32_t size) {
    if (offset > source.size() || size > source.size() - offset || size < sizeof(uint32_t)) {
        return AfsEntryType::unknown;
    }

    const uint32_t magic = read_be<uint32_t>(source.data() + offset);
    if ((magic & adx_signature_mask) == adx_signature_value) {
        return AfsEntryType::adx;
    }
    if (magic == ogg_magic) {
        return AfsEntryType::ogg;
    }
    if (magic == hca_magic) {
        return AfsEntryType::hca;
    }
    return AfsEntryType::unknown;
}

[[nodiscard]] inline std::optional<std::string> parse_name(const uint8_t* bytes, size_t size) {
    const auto* end = std::find(bytes, bytes + size, static_cast<uint8_t>('\0'));
    if (end == bytes) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(end));
}

[[nodiscard]] inline uint32_t first_present_source_offset(std::span<const uint8_t> source) {
    if (source.size() < 8 || !std::equal(afs_magic.begin(), afs_magic.end(), source.begin())) {
        return 0;
    }

    const uint32_t entry_count = read_le<uint32_t>(source.data() + 0x04);
    const uint64_t table_end = 0x08ull + static_cast<uint64_t>(entry_count) * 0x08ull;
    if (table_end > source.size()) {
        return 0;
    }

    for (uint32_t index = 0; index < entry_count; ++index) {
        const size_t entry_offset = 0x08u + static_cast<size_t>(index) * 0x08u;
        const uint32_t offset = read_le<uint32_t>(source.data() + entry_offset + 0x00);
        const uint32_t size = read_le<uint32_t>(source.data() + entry_offset + 0x04);
        if (offset != 0 || size != 0) {
            return offset;
        }
    }

    return 0;
}

[[nodiscard]] inline std::string directory_record_name(const std::optional<std::string>& name) {
    if (!name || name->empty()) {
        return {};
    }

    const std::filesystem::path path(*name);
    const std::string filename = path.filename().string();
    if (filename.size() <= directory_name_size) {
        return filename;
    }

    return filename.substr(0, directory_name_size);
}

} // namespace cricodecs::afs::detail
