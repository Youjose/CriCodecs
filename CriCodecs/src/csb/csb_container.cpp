/**
 * @file csb_container.cpp
 * @brief CSB container object helpers.
 *
 * The CSB object surface follows the vgmstream loader model for CRI UTF cue
 * archives and wrapper payloads. C++23 implementation and verification by
 * Youjose.
 */

#include "csb_container.hpp"

#include <fstream>

namespace cricodecs::csb {

std::filesystem::path CsbStreamInfo::suggested_path() const {
    std::filesystem::path path =
        name.empty()
            ? std::filesystem::path("stream_" + std::to_string(row_index + 1))
            : std::filesystem::path(name);

    if (!path.has_extension()) {
        path += stream_file_extension(format);
    }

    return path;
}

std::expected<std::vector<uint8_t>, std::string> CsbContainer::save() const {
    return std::vector<uint8_t>(m_source.begin(), m_source.end());
}

std::expected<void, std::string> CsbContainer::save_to_file(const std::filesystem::path& output_path) const {
    auto bytes = save();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return std::unexpected("CSB save failed: could not open output file: " + output_path.string());
    }

    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!output.good()) {
        return std::unexpected("CSB save failed: could not write output file: " + output_path.string());
    }

    return {};
}

} // namespace cricodecs::csb
