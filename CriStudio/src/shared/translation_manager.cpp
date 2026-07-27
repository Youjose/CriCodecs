#include "shared/translation_manager.hpp"

#include <QApplication>
#include <QEvent>
#include <QLocale>
#include <QSettings>

#include <cassert>
#include <utility>

namespace cristudio::i18n {

TranslationManager* TranslationManager::s_instance = nullptr;

QString language_code(UiLanguage language) {
    switch (language) {
    case UiLanguage::English:
        return QStringLiteral("en");
    case UiLanguage::Spanish:
        return QStringLiteral("es");
    case UiLanguage::Japanese:
        return QStringLiteral("ja");
    case UiLanguage::ChineseSimplified:
        return QStringLiteral("zh_CN");
    case UiLanguage::ChineseTraditional:
        return QStringLiteral("zh_TW");
    case UiLanguage::System:
    default:
        return QStringLiteral("system");
    }
}

UiLanguage language_from_code(QStringView code) {
    if (code == QStringLiteral("en")) {
        return UiLanguage::English;
    }
    if (code == QStringLiteral("es")) {
        return UiLanguage::Spanish;
    }
    if (code == QStringLiteral("ja")) {
        return UiLanguage::Japanese;
    }
    if (code == QStringLiteral("zh_CN")) {
        return UiLanguage::ChineseSimplified;
    }
    if (code == QStringLiteral("zh_TW")) {
        return UiLanguage::ChineseTraditional;
    }
    return UiLanguage::System;
}

TranslationManager::TranslationManager(QApplication& application)
    : QObject(&application),
      m_application(application) {
    assert(s_instance == nullptr);
    s_instance = this;
    m_application.installEventFilter(this);
}

TranslationManager::~TranslationManager() {
    m_application.removeEventFilter(this);
    if (m_locale) {
        m_application.removeTranslator(m_locale.get());
    }
    m_application.removeTranslator(&m_english);
    s_instance = nullptr;
}

TranslationManager& TranslationManager::instance() {
    assert(s_instance != nullptr);
    return *s_instance;
}

bool TranslationManager::initialize() {
    if (m_english.load(QStringLiteral(":/i18n/cristudio_en.qm"))) {
        m_application.installTranslator(&m_english);
    }
    QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    return set_language_code(
        settings.value(QStringLiteral("ui/language"), QStringLiteral("system")).toString(),
        false
    );
}

bool TranslationManager::set_language(UiLanguage language, bool persist) {
    if (language == m_selected && language != UiLanguage::System) {
        return true;
    }
    const auto previous = m_selected;
    m_selected = language;
    if (!apply_language(language)) {
        m_selected = previous;
        return false;
    }
    if (persist) {
        QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
        settings.setValue(QStringLiteral("ui/language"), language_code(language));
    }
    return true;
}

bool TranslationManager::set_language_code(QStringView code, bool persist) {
    return set_language(language_from_code(code), persist);
}

UiLanguage TranslationManager::selected_language() const noexcept {
    return m_selected;
}

QString TranslationManager::selected_code() const {
    return language_code(m_selected);
}

bool TranslationManager::eventFilter(QObject* watched, QEvent* event) {
    if (
        watched == &m_application &&
        event != nullptr &&
        event->type() == QEvent::LocaleChange &&
        m_selected == UiLanguage::System
    ) {
        (void)apply_language(UiLanguage::System);
    }
    return QObject::eventFilter(watched, event);
}

QString TranslationManager::resource_for(UiLanguage language) const {
    if (language == UiLanguage::System) {
        const auto locale = QLocale::system();
        switch (locale.language()) {
        case QLocale::Spanish:
            language = UiLanguage::Spanish;
            break;
        case QLocale::Japanese:
            language = UiLanguage::Japanese;
            break;
        case QLocale::Chinese:
            language =
                locale.script() == QLocale::TraditionalHanScript ||
                    locale.territory() == QLocale::Taiwan ||
                    locale.territory() == QLocale::HongKong ||
                    locale.territory() == QLocale::Macao
                ? UiLanguage::ChineseTraditional
                : UiLanguage::ChineseSimplified;
            break;
        default:
            language = UiLanguage::English;
            break;
        }
    }

    switch (language) {
    case UiLanguage::Spanish:
        return QStringLiteral(":/i18n/cristudio_es.qm");
    case UiLanguage::Japanese:
        return QStringLiteral(":/i18n/cristudio_ja.qm");
    case UiLanguage::ChineseSimplified:
        return QStringLiteral(":/i18n/cristudio_zh_CN.qm");
    case UiLanguage::ChineseTraditional:
        return QStringLiteral(":/i18n/cristudio_zh_TW.qm");
    case UiLanguage::English:
    case UiLanguage::System:
    default:
        return {};
    }
}

bool TranslationManager::apply_language(UiLanguage language) {
    const auto resource = resource_for(language);
    std::unique_ptr<QTranslator> next;
    if (!resource.isEmpty()) {
        next = std::make_unique<QTranslator>();
        if (!next->load(resource)) {
            return false;
        }
        m_application.installTranslator(next.get());
    }
    if (m_locale) {
        m_application.removeTranslator(m_locale.get());
    }
    m_locale = std::move(next);
    return true;
}

} // namespace cristudio::i18n
