/**
 * @file acx_container.cpp
 * @brief ACX container object helpers.
 *
 * ACX parsing and extraction are grounded in official `adxcat` behavior.
 * The object surface and validation are CriCodecs work by Youjose.
 */

#include "acx_container.hpp"

#include <fstream>

#include "acx_builder.hpp"

namespace cricodecs::acx {

std::filesystem::path AcxEntry::suggested_path(bool include_index_prefix) const {
    const std::string stem = include_index_prefix
        ? ("stream_" + std::to_string(index))
        : std::string("stream");
    return std::filesystem::path(stem + entry_extension(type));
}

std::expected<std::vector<uint8_t>, std::string> AcxContainer::rebuild() const {
    AcxBuildInput input;
    input.entries.reserve(m_entries.size());
    for (uint32_t index = 0; index < m_entries.size(); ++index) {
        auto data = file_data(index);
        if (!data) {
            return std::unexpected(data.error());
        }
        input.entries.push_back(AcxBuildEntry{
            .source_path = {},
            .data = std::vector<uint8_t>(data->begin(), data->end()),
        });
    }

    AcxBuilder builder;
    return builder.build(input);
}

std::expected<void, std::string> AcxContainer::save_to_file(const std::filesystem::path& output_path) const {
    auto bytes = rebuild();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return std::unexpected("ACX save failed: could not open output: " + output_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!output) {
        return std::unexpected("ACX save failed: could not write output: " + output_path.string());
    }
    return {};
}

std::expected<void, std::string> AcxContainer::replace_payloads(std::vector<std::vector<uint8_t>> payloads) {
    AcxBuildInput input;
    input.entries.reserve(payloads.size());
    for (auto& payload : payloads) {
        input.entries.push_back(AcxBuildEntry{.source_path = {}, .data = std::move(payload)});
    }

    AcxBuilder builder;
    auto bytes = builder.build(input);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    m_owned_source = std::move(*bytes);
    m_source = io::SourceView(std::span<const uint8_t>(m_owned_source), {});
    return parse();
}

std::expected<void, std::string> AcxContainer::set_file_data(uint32_t index, std::span<const uint8_t> data) {
    if (index >= m_entries.size()) {
        return std::unexpected("ACX entry index is out of range");
    }

    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(m_entries.size());
    for (uint32_t entry_index = 0; entry_index < m_entries.size(); ++entry_index) {
        if (entry_index == index) {
            payloads.emplace_back(data.begin(), data.end());
            continue;
        }
        auto existing = file_data(entry_index);
        if (!existing) {
            return std::unexpected(existing.error());
        }
        payloads.emplace_back(existing->begin(), existing->end());
    }
    return replace_payloads(std::move(payloads));
}

std::expected<void, std::string> AcxContainer::add_file(std::span<const uint8_t> data) {
    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(m_entries.size() + 1);
    for (uint32_t entry_index = 0; entry_index < m_entries.size(); ++entry_index) {
        auto existing = file_data(entry_index);
        if (!existing) {
            return std::unexpected(existing.error());
        }
        payloads.emplace_back(existing->begin(), existing->end());
    }
    payloads.emplace_back(data.begin(), data.end());
    return replace_payloads(std::move(payloads));
}

std::expected<void, std::string> AcxContainer::remove_file(uint32_t index) {
    if (index >= m_entries.size()) {
        return std::unexpected("ACX entry index is out of range");
    }
    if (m_entries.size() == 1) {
        return std::unexpected("ACX remove failed: archive must keep at least one entry");
    }

    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(m_entries.size() - 1);
    for (uint32_t entry_index = 0; entry_index < m_entries.size(); ++entry_index) {
        if (entry_index == index) {
            continue;
        }
        auto existing = file_data(entry_index);
        if (!existing) {
            return std::unexpected(existing.error());
        }
        payloads.emplace_back(existing->begin(), existing->end());
    }
    return replace_payloads(std::move(payloads));
}

std::expected<void, std::string> AcxContainer::move_file(uint32_t from_index, uint32_t to_index) {
    if (from_index >= m_entries.size() || to_index >= m_entries.size()) {
        return std::unexpected("ACX move failed: entry index is out of range");
    }
    if (from_index == to_index) {
        return {};
    }

    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(m_entries.size());
    for (uint32_t entry_index = 0; entry_index < m_entries.size(); ++entry_index) {
        auto existing = file_data(entry_index);
        if (!existing) {
            return std::unexpected(existing.error());
        }
        payloads.emplace_back(existing->begin(), existing->end());
    }

    auto moved = std::move(payloads[from_index]);
    payloads.erase(payloads.begin() + static_cast<std::ptrdiff_t>(from_index));
    payloads.insert(payloads.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(moved));
    return replace_payloads(std::move(payloads));
}

} // namespace cricodecs::acx
