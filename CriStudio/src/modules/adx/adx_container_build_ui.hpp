#pragma once

#include "modules/aax/aax_edit.hpp"
#include "modules/aix/aix_edit.hpp"

#include <QString>

#include <expected>
#include <optional>
#include <variant>

class QWidget;

namespace cristudio::modules::adx {

using AdxContainerBuildConfig = std::variant<modules::aax::BuildConfig, modules::aix::BuildConfig>;

[[nodiscard]] std::expected<std::optional<AdxContainerBuildConfig>, QString> choose_container_build_config(
    QWidget* parent,
    QString title,
    bool aix_target
);

} // namespace cristudio::modules::adx
