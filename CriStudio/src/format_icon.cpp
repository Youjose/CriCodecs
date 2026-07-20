#include "format_icon.hpp"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>

#include <array>
#include <unordered_map>

namespace cristudio {
namespace {

QString icon_token(QString type) {
    const auto lowered = type.toLower();

    for (const auto& token : {
             QStringLiteral("CPK"), QStringLiteral("ACB"), QStringLiteral("AWB"), QStringLiteral("ADX"),
             QStringLiteral("AHX"), QStringLiteral("AAX"), QStringLiteral("AIX"), QStringLiteral("HCA"),
             QStringLiteral("USM"), QStringLiteral("SFD"), QStringLiteral("CSB"), QStringLiteral("AFS"),
             QStringLiteral("CVM"), QStringLiteral("UTF")
         }) {
        if (lowered.contains(token.toLower())) {
            return token;
        }
    }

    if (lowered.contains(QStringLiteral("video"))) {
        return QStringLiteral("VID");
    }
    if (lowered.contains(QStringLiteral("audio"))) {
        return QStringLiteral("AUD");
    }
    return QStringLiteral("DAT");
}

QColor token_color(const QString& token) {
    if (token == QStringLiteral("AUD") || token == QStringLiteral("WAV") ||
        token == QStringLiteral("ADX") || token == QStringLiteral("AHX") ||
        token == QStringLiteral("AAX") || token == QStringLiteral("AIX") ||
        token == QStringLiteral("HCA")) {
        return QColor(53, 139, 214);
    }
    if (token == QStringLiteral("VID") || token == QStringLiteral("USM") || token == QStringLiteral("SFD")) {
        return QColor(186, 92, 190);
    }
    if (token == QStringLiteral("CPK") || token == QStringLiteral("AFS") || token == QStringLiteral("CVM")) {
        return QColor(42, 150, 105);
    }
    if (token == QStringLiteral("ACB") || token == QStringLiteral("AWB") || token == QStringLiteral("CSB")) {
        return QColor(215, 132, 44);
    }
    if (token == QStringLiteral("UTF")) {
        return QColor(101, 119, 203);
    }
    return QColor(104, 116, 128);
}

QIcon make_badge_icon(const QString& token) {
    constexpr int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    const auto accent = token_color(token);
    const auto palette = QApplication::palette();
    const auto surface = palette.color(QPalette::Base);
    const auto text = palette.color(QPalette::Text);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(accent.darker(115), 1.4));
    painter.setBrush(surface);
    painter.drawRoundedRect(QRectF(6.0, 3.5, 18.5, 25.0), 3.0, 3.0);

    QPolygonF fold;
    fold << QPointF(19.0, 3.5) << QPointF(24.5, 9.0) << QPointF(19.0, 9.0);
    painter.setBrush(accent.lighter(130));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(fold);

    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(4.5, 17.0, 23.0, 10.0), 2.5, 2.5);

    QFont font = QApplication::font();
    font.setBold(true);
    font.setPointSize(token.size() <= 3 ? 6 : 5);
    painter.setFont(font);
    painter.setPen(accent.lightness() < 130 ? Qt::white : text);
    painter.drawText(QRectF(4.5, 16.5, 23.0, 10.5), Qt::AlignCenter, token.left(3));

    return QIcon(pixmap);
}

QIcon make_audio_icon() {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    const QColor accent(53, 139, 214);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(6.0, 12.0, 6.0, 8.0), 1.5, 1.5);
    QPolygonF cone;
    cone << QPointF(12.0, 12.0) << QPointF(20.0, 7.0) << QPointF(20.0, 25.0) << QPointF(12.0, 20.0);
    painter.drawPolygon(cone);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(18.0, 10.0, 8.0, 12.0), -45 * 16, 90 * 16);
    return QIcon(pixmap);
}

QIcon make_video_icon() {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    const QColor accent(186, 92, 190);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(accent.darker(120), 1.4));
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(5.0, 8.0, 22.0, 16.0), 3.0, 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    QPolygonF play;
    play << QPointF(14.0, 12.0) << QPointF(14.0, 20.0) << QPointF(21.0, 16.0);
    painter.drawPolygon(play);
    return QIcon(pixmap);
}

} // namespace

QIcon make_entry_icon(const QString& type, const QString& name, bool folder) {
    if (folder) {
        static const QIcon folder_icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        return folder_icon;
    }

    auto token = icon_token(type);
    if (token == QStringLiteral("DAT") || token == QStringLiteral("AUD") || token == QStringLiteral("VID")) {
        token = icon_token(type + QLatin1Char(' ') + name);
    }
    static std::unordered_map<std::string, QIcon> icon_cache;
    const auto key = token.toStdString();
    if (auto it = icon_cache.find(key); it != icon_cache.end()) {
        return it->second;
    }

    QIcon icon;
    if (token == QStringLiteral("AUD") || token == QStringLiteral("WAV")) {
        icon = make_audio_icon();
    } else if (token == QStringLiteral("VID")) {
        icon = make_video_icon();
    } else {
        icon = make_badge_icon(token);
    }
    icon_cache.emplace(key, icon);
    return icon;
}

QIcon make_document_icon(const QString& format, const QString& name) {
    return make_entry_icon(format, name, false);
}

} // namespace cristudio
