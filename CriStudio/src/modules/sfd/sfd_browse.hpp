#pragma once

#include "document/document_types.hpp"

#include "sfd_container.hpp"

#include <filesystem>

namespace cristudio::modules::sfd {

[[nodiscard]] LoadedDocument summarize(
    const std::filesystem::path& path,
    const cricodecs::sfd::SfdContainer& sfd
);

} // namespace cristudio::modules::sfd
