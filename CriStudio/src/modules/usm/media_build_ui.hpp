#pragma once

#include "modules/usm/media_build.hpp"

#include <QString>

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

class QWidget;

namespace cristudio::modules::usm {

[[nodiscard]] std::expected<std::optional<MediaBuildConfig>, QString> choose_media_build_config(
    QWidget* parent,
    QString title,
    DecryptionKeys keys,
    bool prefer_sfd,
    const cricodecs::usm::UsmReader* current_usm = nullptr,
    const std::filesystem::path& current_usm_path = {},
    std::span<const uint8_t> current_usm_bytes = {}
);

} // namespace cristudio::modules::usm
