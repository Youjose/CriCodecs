#pragma once

#include "document/document_types.hpp"

#include "wav_container.hpp"

#include <filesystem>

namespace cristudio::modules::wav {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::wav::WavContainer& wav
);

} // namespace cristudio::modules::wav
