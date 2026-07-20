#pragma once

#include "modules/csb/csb_edit.hpp"

#include <QString>

#include <expected>
#include <optional>

class QWidget;

namespace cristudio::modules::csb {

[[nodiscard]] std::expected<std::optional<DirectoryBuildConfig>, QString> choose_directory_build_config(
    QWidget* parent,
    QString title
);

} // namespace cristudio::modules::csb
