#pragma once

#include "document/document_types.hpp"

#include "acx_container.hpp"

#include <filesystem>

namespace cristudio::modules::acx {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::acx::AcxContainer& acx
);

} // namespace cristudio::modules::acx
