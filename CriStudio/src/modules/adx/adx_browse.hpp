#pragma once

#include "document/document_types.hpp"

#include "adx_codec.hpp"

#include <filesystem>

namespace cristudio::modules::adx {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::adx::Adx& adx
);

} // namespace cristudio::modules::adx
