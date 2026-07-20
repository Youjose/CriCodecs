#pragma once

#include "document/document_types.hpp"

#include "aax_container.hpp"

#include <filesystem>

namespace cristudio::modules::aax {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::aax::AaxContainer& aax
);

} // namespace cristudio::modules::aax
