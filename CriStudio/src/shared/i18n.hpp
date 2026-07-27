#pragma once

#include <QCoreApplication>

#include <string>

namespace cristudio::i18n {

[[nodiscard]] inline std::string translate_utf8(
    const char* context,
    const char* source,
    int count = -1) {
    const auto utf8 = QCoreApplication::translate(context, source, nullptr, count).toUtf8();
    return {utf8.constData(), static_cast<size_t>(utf8.size())};
}

} // namespace cristudio::i18n
