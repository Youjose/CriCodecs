#pragma once

#include "document/document_types.hpp"

#include "cpk_container.hpp"

#include <filesystem>

namespace cristudio::modules::cpk {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::cpk::Cpk& cpk
);

} // namespace cristudio::modules::cpk
