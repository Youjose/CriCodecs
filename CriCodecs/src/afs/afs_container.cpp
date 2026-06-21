/**
 * @file afs_container.cpp
 * @brief Classic AFS container object helpers.
 *
 * The object model is grounded in CRI AfsLink/afslnk behavior.
 * CriCodecs C++23 implementation and verification by Youjose.
 */

#include "afs_container.hpp"

#include <algorithm>

#include "afs_format.hpp"
#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::afs {

namespace {

using util::align_up_checked;

} // namespace

std::filesystem::path AfsEntry::suggested_path(bool include_index_prefix) const {
    std::string stem;
    if (name && !name->empty()) {
        stem = *name;
    } else if (include_index_prefix) {
        stem = "entry_" + std::to_string(index);
    } else {
        stem = "entry";
    }

    std::filesystem::path path(stem);
    if (!path.has_extension()) {
        path += entry_extension(type);
    }
    return path;
}

std::optional<AfsDirectoryTimestamp> AfsEntry::directory_timestamp() const noexcept {
    AfsDirectoryTimestamp timestamp{
        .year = io::read_le<uint16_t>(directory_metadata.data() + 0),
        .month = io::read_le<uint16_t>(directory_metadata.data() + 2),
        .day = io::read_le<uint16_t>(directory_metadata.data() + 4),
        .hour = io::read_le<uint16_t>(directory_metadata.data() + 6),
        .minute = io::read_le<uint16_t>(directory_metadata.data() + 8),
        .second = io::read_le<uint16_t>(directory_metadata.data() + 10),
    };
    if (timestamp.empty()) {
        return std::nullopt;
    }
    return timestamp;
}

std::array<uint8_t, 12> encode_directory_timestamp(const AfsDirectoryTimestamp& timestamp) noexcept {
    std::array<uint8_t, 12> bytes{};
    io::write_le<uint16_t>(bytes.data() + 0, timestamp.year);
    io::write_le<uint16_t>(bytes.data() + 2, timestamp.month);
    io::write_le<uint16_t>(bytes.data() + 4, timestamp.day);
    io::write_le<uint16_t>(bytes.data() + 6, timestamp.hour);
    io::write_le<uint16_t>(bytes.data() + 8, timestamp.minute);
    io::write_le<uint16_t>(bytes.data() + 10, timestamp.second);
    return bytes;
}

uint32_t AfsContainer::present_entry_count() const noexcept {
    uint32_t count = 0;
    for (const auto& entry : m_entries) {
        if (entry.present) {
            ++count;
        }
    }
    return count;
}

void AfsContainer::add_file(
    std::span<const uint8_t> data,
    std::optional<std::string> name,
    const std::array<uint8_t, 12>& directory_metadata
) {
    add_file_at_id(static_cast<uint32_t>(m_entries.size()), data, std::move(name), directory_metadata);
}

void AfsContainer::add_file_at_id(
    uint32_t file_id,
    std::span<const uint8_t> data,
    std::optional<std::string> name,
    const std::array<uint8_t, 12>& directory_metadata
) {
    reserve_file_id(file_id);
    if (m_file_data.size() < m_entries.size()) {
        m_file_data.resize(m_entries.size());
    }
    if (m_file_data_overrides.size() < m_entries.size()) {
        m_file_data_overrides.resize(m_entries.size(), 0);
    }

    AfsEntry& entry = m_entries[file_id];
    entry.index = file_id;
    entry.offset = 0;
    entry.size = static_cast<uint32_t>(data.size());
    entry.present = true;
    entry.type = detail::detect_entry_type(data, 0, entry.size);
    entry.name = std::move(name);
    entry.header_source_name.reset();
    entry.directory_metadata = directory_metadata;

    m_file_data[file_id].assign(data.begin(), data.end());
    m_file_data_overrides[file_id] = 1;
}

void AfsContainer::reserve_file_id(uint32_t file_id) {
    if (file_id < m_entries.size()) {
        return;
    }

    const size_t previous_size = m_entries.size();
    m_entries.resize(static_cast<size_t>(file_id) + 1u);
    m_file_data.resize(m_entries.size());
    m_file_data_overrides.resize(m_entries.size(), 0);
    for (size_t index = previous_size; index < m_entries.size(); ++index) {
        m_entries[index].index = static_cast<uint32_t>(index);
        m_entries[index].offset = 0;
        m_entries[index].size = 0;
        m_entries[index].present = false;
        m_entries[index].type = AfsEntryType::unknown;
        m_entries[index].name.reset();
        m_entries[index].header_source_name.reset();
        m_entries[index].directory_metadata.fill(0);
    }
}

bool AfsContainer::is_materialized() const noexcept {
    if (m_entries.empty() || m_file_data.size() != m_entries.size() ||
        m_file_data_overrides.size() != m_entries.size()) {
        return false;
    }
    return std::ranges::all_of(m_entries, [this](const AfsEntry& entry) {
        return !entry.present || m_file_data_overrides[entry.index] != 0;
    });
}

std::expected<void, std::string> AfsContainer::materialize() {
    if (m_entries.empty()) {
        return std::unexpected("AFS materialize failed: no entries are present");
    }
    if (m_source.empty()) {
        return std::unexpected("AFS materialize failed: source data is empty");
    }

    m_file_data.clear();
    m_file_data_overrides.clear();
    m_file_data.reserve(m_entries.size());
    m_file_data_overrides.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        if (!entry.present) {
            m_file_data.emplace_back();
            m_file_data_overrides.push_back(0);
            continue;
        }
        if (entry.offset > m_source.size() || entry.size > m_source.size() - entry.offset) {
            return std::unexpected("AFS materialize failed: entry offset/size is out of range");
        }
        m_file_data.emplace_back(
            m_source.begin() + static_cast<std::ptrdiff_t>(entry.offset),
            m_source.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size)
        );
        m_file_data_overrides.push_back(1);
    }

    return {};
}

std::expected<std::span<const uint8_t>, std::string> AfsContainer::build_payload(uint32_t index) const {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS entry index is out of range");
    }

    const auto& entry = m_entries[index];
    if (!entry.present) {
        return std::unexpected("AFS entry slot is empty");
    }

    if (index < m_file_data_overrides.size() && m_file_data_overrides[index] != 0) {
        if (index >= m_file_data.size()) {
            return std::unexpected("AFS build failed: entry payload is missing");
        }
        return std::span<const uint8_t>(m_file_data[index]);
    }

    return file_data(index);
}

std::expected<void, std::string> AfsContainer::replace_file(uint32_t index, std::span<const uint8_t> data) {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS replace_file failed: entry index is out of range");
    }
    if (m_file_data.size() < m_entries.size()) {
        m_file_data.resize(m_entries.size());
    }
    if (m_file_data_overrides.size() < m_entries.size()) {
        m_file_data_overrides.resize(m_entries.size(), 0);
    }

    m_file_data[index].assign(data.begin(), data.end());
    m_file_data_overrides[index] = 1;
    m_entries[index].offset = 0;
    m_entries[index].size = static_cast<uint32_t>(data.size());
    m_entries[index].present = true;
    m_entries[index].type = detail::detect_entry_type(data, 0, static_cast<uint32_t>(data.size()));
    return {};
}

std::expected<void, std::string> AfsContainer::remove_file(uint32_t index) {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS remove_file failed: entry index is out of range");
    }
    if (!m_entries[index].present) {
        return {};
    }
    if (present_entry_count() <= 1) {
        return std::unexpected("AFS remove_file failed: archive must keep at least one populated entry");
    }

    if (m_file_data.size() < m_entries.size()) {
        m_file_data.resize(m_entries.size());
    }
    if (m_file_data_overrides.size() < m_entries.size()) {
        m_file_data_overrides.resize(m_entries.size(), 0);
    }

    auto& entry = m_entries[index];
    entry.offset = 0;
    entry.size = 0;
    entry.present = false;
    entry.type = AfsEntryType::unknown;
    entry.name.reset();
    entry.header_source_name.reset();
    entry.directory_metadata.fill(0);
    m_file_data[index].clear();
    m_file_data_overrides[index] = 0;
    return {};
}

std::expected<void, std::string> AfsContainer::move_file(uint32_t from_index, uint32_t to_index) {
    if (from_index >= m_entries.size() || to_index >= m_entries.size()) {
        return std::unexpected("AFS move_file failed: entry index is out of range");
    }
    if (from_index == to_index) {
        return {};
    }

    if (m_file_data.size() < m_entries.size()) {
        m_file_data.resize(m_entries.size());
    }
    if (m_file_data_overrides.size() < m_entries.size()) {
        m_file_data_overrides.resize(m_entries.size(), 0);
    }

    auto entry = std::move(m_entries[from_index]);
    auto payload = std::move(m_file_data[from_index]);
    auto override_flag = m_file_data_overrides[from_index];
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_file_data.erase(m_file_data.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_file_data_overrides.erase(m_file_data_overrides.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(entry));
    m_file_data.insert(m_file_data.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(payload));
    m_file_data_overrides.insert(m_file_data_overrides.begin() + static_cast<std::ptrdiff_t>(to_index), override_flag);

    for (size_t index = 0; index < m_entries.size(); ++index) {
        m_entries[index].index = static_cast<uint32_t>(index);
    }
    return {};
}

std::expected<void, std::string> AfsContainer::set_name(uint32_t index, std::optional<std::string> name) {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS set_name failed: entry index is out of range");
    }

    m_entries[index].name = std::move(name);
    return {};
}

std::expected<void, std::string> AfsContainer::set_header_source_name(
    uint32_t index,
    std::optional<std::string> header_source_name
) {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS set_header_source_name failed: entry index is out of range");
    }

    m_entries[index].header_source_name = std::move(header_source_name);
    return {};
}

std::expected<void, std::string> AfsContainer::set_directory_metadata(
    uint32_t index,
    const std::array<uint8_t, 12>& metadata
) {
    if (index >= m_entries.size()) {
        return std::unexpected("AFS set_directory_metadata failed: entry index is out of range");
    }

    m_entries[index].directory_metadata = metadata;
    return {};
}

std::expected<void, std::string> AfsContainer::set_directory_timestamp(
    uint32_t index,
    std::optional<AfsDirectoryTimestamp> timestamp
) {
    if (!timestamp.has_value()) {
        return set_directory_metadata(index, {});
    }
    return set_directory_metadata(index, encode_directory_timestamp(*timestamp));
}

std::expected<void, std::string> AfsContainer::set_alignment(uint32_t alignment) {
    if (alignment == 0) {
        return std::unexpected("AFS alignment must be non-zero");
    }

    if (m_first_payload_offset.has_value()) {
        auto aligned_offset = align_up_checked(
            *m_first_payload_offset,
            alignment,
            "AFS alignment update failed"
        );
        if (!aligned_offset) {
            return std::unexpected(aligned_offset.error());
        }
        m_first_payload_offset = *aligned_offset;
    }

    m_alignment = alignment;
    return {};
}

std::expected<void, std::string> AfsContainer::set_first_payload_offset(std::optional<uint32_t> offset) {
    if (offset.has_value() && *offset == 0) {
        return std::unexpected("AFS first payload offset must be non-zero when provided");
    }

    if (!offset.has_value()) {
        m_first_payload_offset.reset();
        return {};
    }

    auto aligned_offset = align_up_checked(
        *offset,
        m_alignment,
        "AFS first payload offset update failed"
    );
    if (!aligned_offset) {
        return std::unexpected(aligned_offset.error());
    }

    m_first_payload_offset = *aligned_offset;
    return {};
}

} // namespace cricodecs::afs
