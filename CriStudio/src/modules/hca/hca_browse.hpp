#pragma once

#include "document/document_types.hpp"

#include "hca_codec.hpp"

#include <filesystem>

namespace cristudio::modules::hca {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::hca::Hca& hca
);

} // namespace cristudio::modules::hca
