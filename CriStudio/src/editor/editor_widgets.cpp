#include "editor/editor_widgets.hpp"

#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QTabBar>
#include <QTabWidget>

#include <utility>

namespace cristudio {

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Toggle switch"));
}

QSize ToggleSwitch::sizeHint() const {
    return {46, 24};
}

QSize ToggleSwitch::minimumSizeHint() const {
    return sizeHint();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto track = rect().adjusted(1, 2, -1, -2);
    const auto track_color = isChecked()
        ? palette().color(QPalette::Highlight)
        : palette().color(QPalette::Mid);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track_color);
    painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

    constexpr int margin = 3;
    const int diameter = track.height() - margin * 2;
    const int x = isChecked()
        ? track.right() - margin - diameter + 1
        : track.left() + margin;
    painter.setBrush(isChecked()
        ? palette().color(QPalette::HighlightedText)
        : palette().color(QPalette::ButtonText));
    painter.drawEllipse(QRect(x, track.top() + margin, diameter, diameter));

    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = rect();
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
    }
}

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel* value_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("ValueLabel"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

void add_editor_start_tab(QTabWidget* tabs) {
    auto* empty = new QLabel(QStringLiteral("Open files or archive entries in the Editor from the browser context menus."), tabs);
    empty->setObjectName(QStringLiteral("DocumentSubtitle"));
    empty->setAlignment(Qt::AlignCenter);
    tabs->addTab(empty, QStringLiteral("Start"));
    tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
}

void remove_editor_tab(QTabWidget* tabs, QWidget* widget) {
    if (tabs == nullptr || widget == nullptr) {
        return;
    }
    const auto index = tabs->indexOf(widget);
    if (index < 0) {
        return;
    }
    tabs->removeTab(index);
    widget->deleteLater();
    if (tabs->count() == 0) {
        add_editor_start_tab(tabs);
    }
}

} // namespace cristudio
