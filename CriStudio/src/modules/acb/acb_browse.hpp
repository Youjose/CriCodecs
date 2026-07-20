#pragma once

#include "document/document_types.hpp"

#include "acb_container.hpp"

#include <filesystem>

namespace cristudio::modules::acb {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::acb::AcbContainer& acb
);

} // namespace cristudio::modules::acb
