#pragma once

#include "modules/audio/audio_encode.hpp"

#include <QString>

#include <expected>
#include <optional>

class QWidget;

namespace cristudio::modules::audio {

[[nodiscard]] std::expected<std::optional<EncodeConfig>, QString> choose_encode_config(
    QWidget* parent,
    QString title,
    DecryptionKeys keys,
    EncodeTarget preferred_target
);

} // namespace cristudio::modules::audio
