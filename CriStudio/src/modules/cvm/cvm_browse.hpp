#pragma once

#include "document/document_types.hpp"

#include "cvm_container.hpp"

#include <filesystem>

namespace cristudio::modules::cvm {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::cvm::CvmContainer& cvm
);

} // namespace cristudio::modules::cvm
