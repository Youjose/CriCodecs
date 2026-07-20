#pragma once

#include "document/document_types.hpp"

#include "aix_container.hpp"

#include <filesystem>

namespace cristudio::modules::aix {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::aix::Aix& aix
);

} // namespace cristudio::modules::aix
