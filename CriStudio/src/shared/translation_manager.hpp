#pragma once

#include <QObject>
#include <QString>
#include <QStringView>
#include <QTranslator>

#include <memory>

class QApplication;
class QEvent;

namespace cristudio::i18n {

enum class UiLanguage {
    System,
    English,
    Spanish,
    Japanese,
    ChineseSimplified,
    ChineseTraditional,
};

[[nodiscard]] QString language_code(UiLanguage language);
[[nodiscard]] UiLanguage language_from_code(QStringView code);

class TranslationManager final : public QObject {
public:
    explicit TranslationManager(QApplication& application);
    ~TranslationManager() override;

    TranslationManager(const TranslationManager&) = delete;
    TranslationManager& operator=(const TranslationManager&) = delete;

    [[nodiscard]] static TranslationManager& instance();

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool set_language(UiLanguage language, bool persist = true);
    [[nodiscard]] bool set_language_code(QStringView code, bool persist = true);
    [[nodiscard]] UiLanguage selected_language() const noexcept;
    [[nodiscard]] QString selected_code() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QString resource_for(UiLanguage language) const;
    [[nodiscard]] bool apply_language(UiLanguage language);

    static TranslationManager* s_instance;

    QApplication& m_application;
    QTranslator m_english;
    std::unique_ptr<QTranslator> m_locale;
    UiLanguage m_selected = UiLanguage::System;
};

} // namespace cristudio::i18n
