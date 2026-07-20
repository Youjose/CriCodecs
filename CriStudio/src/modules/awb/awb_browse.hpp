#pragma once

#include "document/document_types.hpp"

#include "awb_container.hpp"

#include <filesystem>

namespace cristudio::modules::awb {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::awb::AwbContainer& awb
);

} // namespace cristudio::modules::awb
