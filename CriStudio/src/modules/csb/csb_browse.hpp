#pragma once

#include "document/document_types.hpp"

#include "csb_container.hpp"

#include <filesystem>

namespace cristudio::modules::csb {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::csb::CsbContainer& csb
);

} // namespace cristudio::modules::csb
