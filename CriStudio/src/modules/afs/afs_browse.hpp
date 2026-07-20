#pragma once

#include "document/document_types.hpp"

#include "afs_container.hpp"

#include <filesystem>

namespace cristudio::modules::afs {

[[nodiscard]] std::string timestamp_text(const cricodecs::afs::AfsDirectoryTimestamp& timestamp);

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::afs::AfsContainer& afs
);

} // namespace cristudio::modules::afs
