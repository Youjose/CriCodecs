#include "main_window.hpp"
#include "path_text.hpp"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QProxyStyle>
#include <QSettings>
#include <QStyle>
#include <QStringList>
#include <QWidget>

#include <filesystem>
#include <vector>

namespace {

bool g_show_shortcut_underlines = false;

void refresh_shortcut_underlines() {
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget != nullptr) {
            widget->update();
        }
    }
}

void set_shortcut_underlines_visible(bool visible) {
    if (g_show_shortcut_underlines == visible) {
        return;
    }
    g_show_shortcut_underlines = visible;
    refresh_shortcut_underlines();
}

bool is_menu_surface(QObject* object) {
    for (auto* current = object; current != nullptr; current = current->parent()) {
        if (qobject_cast<QMenu*>(current) != nullptr || qobject_cast<QMenuBar*>(current) != nullptr) {
            return true;
        }
    }
    return false;
}

class ShortcutUnderlineStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int pixelMetric(
        PixelMetric metric,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr) const override {
        if (metric == QStyle::PM_HeaderGripMargin) {
            return 8;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    int styleHint(
        StyleHint hint,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr,
        QStyleHintReturn* return_data = nullptr) const override {
        if (hint == QStyle::SH_UnderlineShortcut) {
            return g_show_shortcut_underlines || qApp->property("alwaysShowAccessKeys").toBool() ? 1 : 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }
};

class ShortcutUnderlineController final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event == nullptr) {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type()) {
        case QEvent::KeyPress: {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Alt) {
                set_shortcut_underlines_visible(true);
            } else if (key->key() == Qt::Key_Escape) {
                set_shortcut_underlines_visible(false);
            }
            break;
        }
        case QEvent::Hide:
        case QEvent::Close:
            if (qobject_cast<QMenu*>(watched) != nullptr) {
                set_shortcut_underlines_visible(false);
            }
            break;
        case QEvent::ApplicationDeactivate:
            set_shortcut_underlines_visible(false);
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
            if (!is_menu_surface(watched)) {
                set_shortcut_underlines_visible(false);
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }
};

void configure_application_font() {
    const QStringList candidate_font_files = {
        QStringLiteral("/mnt/c/Windows/Fonts/YuGothR.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/YuGothM.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/msgothic.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/msyh.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/simsun.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/mingliub.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/malgun.ttf")
    };

    QStringList fallback_families;
    for (const auto& path : candidate_font_files) {
        if (!QFileInfo::exists(path)) {
            continue;
        }
        const auto id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            continue;
        }
        fallback_families.append(QFontDatabase::applicationFontFamilies(id));
    }

    QFont font = QApplication::font();
    QStringList families;
    families.push_back(font.family());
    for (const auto& family : fallback_families) {
        if (!families.contains(family)) {
            families.push_back(family);
        }
    }
    for (const auto& family : {
             QStringLiteral("Yu Gothic"),
             QStringLiteral("MS Gothic"),
             QStringLiteral("Microsoft YaHei"),
             QStringLiteral("SimSun"),
             QStringLiteral("MingLiU"),
             QStringLiteral("Malgun Gothic"),
             QStringLiteral("Noto Sans CJK JP"),
             QStringLiteral("Noto Sans CJK SC")
         }) {
        if (!families.contains(family)) {
            families.push_back(family);
        }
    }
    font.setFamilies(families);
    if (font.pointSizeF() > 0.0 && font.pointSizeF() < 10.0) {
        font.setPointSizeF(10.0);
    }
    QApplication::setFont(font);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setStyle(new ShortcutUnderlineStyle(QApplication::style()->name()));
    ShortcutUnderlineController shortcut_underlines;
    app.installEventFilter(&shortcut_underlines);
    QApplication::setApplicationName(QStringLiteral("CriStudio"));
    QApplication::setOrganizationName(QStringLiteral("CriCodecs"));
    QApplication::setApplicationVersion(QStringLiteral(CRISTUDIO_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/cristudio.png")));
    QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    app.setProperty("alwaysShowAccessKeys", settings.value(QStringLiteral("ui/alwaysShowAccessKeys"), false));
    configure_application_font();

    cristudio::MainWindow window;
    window.setWindowIcon(QApplication::windowIcon());
    window.show();
    std::vector<std::filesystem::path> startup_paths;
    const auto arguments = QApplication::arguments();
    if (arguments.size() > 1) {
        startup_paths.reserve(static_cast<size_t>(arguments.size() - 1));
    }
    for (int i = 1; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument.startsWith(QLatin1Char('-'))) {
            continue;
        }
        startup_paths.emplace_back(cristudio::path_from_qstring(argument));
    }
    window.load_startup_paths(std::move(startup_paths));

    return QApplication::exec();
}
