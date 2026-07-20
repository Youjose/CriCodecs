#pragma once

#include "document/document_types.hpp"

#include "utf_table.hpp"

#include <filesystem>

namespace cristudio::modules::utf {

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::utf::UtfTable& utf);

} // namespace cristudio::modules::utf
