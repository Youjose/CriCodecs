#pragma once

#include <QIcon>
#include <QString>

namespace cristudio {

[[nodiscard]] QIcon make_entry_icon(const QString& type, const QString& name, bool folder);
[[nodiscard]] QIcon make_document_icon(const QString& format, const QString& name);

} // namespace cristudio
