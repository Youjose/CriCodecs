#include "browser_delegates.hpp"

#include "entry_table_model.hpp"
#include "file_list_model.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeView>

#include <algorithm>
#include <cmath>

namespace cristudio {
namespace {

QString search_text_from(const QLineEdit* edit) {
    return edit == nullptr ? QString{} : edit->text().trimmed();
}

QString search_text_from(const QLineEdit* primary, const QLineEdit* fallback) {
    auto text = search_text_from(primary);
    return text.isEmpty() ? search_text_from(fallback) : text;
}

QColor mixed_color(QColor a, QColor b, qreal t) {
    return QColor(
        static_cast<int>(std::lerp(a.redF(), b.redF(), t) * 255.0),
        static_cast<int>(std::lerp(a.greenF(), b.greenF(), t) * 255.0),
        static_cast<int>(std::lerp(a.blueF(), b.blueF(), t) * 255.0),
        static_cast<int>(std::lerp(a.alphaF(), b.alphaF(), t) * 255.0)
    );
}

void draw_highlighted_text(
    QPainter& painter,
    QRect rect,
    QString text,
    const QFont& font,
    const QColor& color,
    const QColor& highlight,
    const QString& query,
    int flags = Qt::AlignVCenter | Qt::AlignLeft
) {
    painter.save();
    painter.setClipRect(rect);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const auto elided = metrics.elidedText(text, Qt::ElideMiddle, rect.width());
    const auto needle = query.trimmed();
    const auto match = needle.isEmpty() ? -1 : elided.indexOf(needle, 0, Qt::CaseInsensitive);
    if (match < 0) {
        painter.setPen(color);
        painter.drawText(rect, flags, elided);
        painter.restore();
        return;
    }

    const auto prefix = elided.left(match);
    const auto found = elided.mid(match, needle.size());
    const auto suffix = elided.mid(match + needle.size());
    const auto base_y = rect.y() + (rect.height() + metrics.ascent() - metrics.descent()) / 2;
    auto x = rect.x();

    painter.setPen(color);
    painter.drawText(QPoint(x, base_y), prefix);
    x += metrics.horizontalAdvance(prefix);

    const auto highlight_width = metrics.horizontalAdvance(found);
    const QRect highlight_rect(x - 1, rect.y() + 3, highlight_width + 3, rect.height() - 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    painter.drawRoundedRect(highlight_rect, 3, 3);
    painter.setPen(color);
    painter.drawText(QPoint(x, base_y), found);
    x += highlight_width;
    painter.drawText(QPoint(x, base_y), suffix);
    painter.restore();
}

void draw_selected_row(
    QPainter& painter,
    QRect rect,
    const QColor& fill,
    const QColor& outline,
    bool active
) {
    rect = rect.adjusted(0, 1, 0, -1);
    painter.fillRect(rect, fill);
    if (active) {
        painter.setPen(QPen(outline, 1.0));
        painter.drawLine(rect.topLeft(), rect.topRight());
        painter.drawLine(rect.bottomLeft(), rect.bottomRight());
    }
}

bool row_is_selected(const QTreeView* view, const QModelIndex& index, bool fallback) {
    if (fallback) {
        return true;
    }
    if (view == nullptr || view->selectionModel() == nullptr || !index.isValid()) {
        return false;
    }
    return view->selectionModel()->isSelected(index.sibling(index.row(), 0));
}

bool row_is_hovered(const QTreeView* view, const QModelIndex& index, bool fallback) {
    (void)view;
    (void)index;
    return fallback;
}

} // namespace

LoadedFileDelegate::LoadedFileDelegate(QLineEdit* filter, QObject* parent)
    : QStyledItemDelegate(parent), m_filter(filter) {}

QSize LoadedFileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const {
    auto title_font = option.font;
    title_font.setWeight(QFont::DemiBold);
    auto meta_font = option.font;
    if (meta_font.pointSizeF() > 0.0) {
        meta_font.setPointSizeF(std::max(8.0, meta_font.pointSizeF() - 1.0));
    } else if (meta_font.pixelSize() > 0) {
        meta_font.setPixelSize(std::max(10, meta_font.pixelSize() - 1));
    }

    const QFontMetrics title_metrics(title_font);
    const QFontMetrics meta_metrics(meta_font);
    const auto content_height = m_compact
        ? title_metrics.height()
        : std::max(24, title_metrics.height() + meta_metrics.height());
    return QSize(240, content_height + 8);
}

void LoadedFileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const auto selected = option.state.testFlag(QStyle::State_Selected);
        const auto hovered = option.state.testFlag(QStyle::State_MouseOver);
        const auto focused = option.state.testFlag(QStyle::State_HasFocus);
        const auto palette = option.palette;
        const auto accent = palette.color(QPalette::Highlight);
        const auto base = palette.color(QPalette::Base);
        const auto alt = palette.color(QPalette::AlternateBase);
        const auto text = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text);
        const auto muted = selected
            ? mixed_color(text, accent, 0.18)
            : palette.color(QPalette::Mid);

        QRect card = option.rect.adjusted(3, 1, -3, -1);
        const auto fill = selected ? accent : (hovered ? alt : base);
        painter->setPen(QPen(selected || focused ? accent : QColor(0, 0, 0, 0), selected || focused ? 1.2 : 0.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(card, 3, 3);

        constexpr auto icon_size = 24;
        const QRect icon_rect(card.left() + 8, card.top() + (card.height() - icon_size) / 2, icon_size, icon_size);
        if (!m_compact) {
            qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).paint(painter, icon_rect);
        }

        const auto text_left = m_compact ? card.left() + 8 : icon_rect.right() + 9;
        auto title_font = option.font;
        title_font.setWeight(QFont::DemiBold);
        auto meta_font = option.font;
        if (meta_font.pointSizeF() > 0.0) {
            meta_font.setPointSizeF(std::max(8.0, meta_font.pointSizeF() - 1.0));
        } else if (meta_font.pixelSize() > 0) {
            meta_font.setPixelSize(std::max(10, meta_font.pixelSize() - 1));
        }
        const QFontMetrics title_metrics(title_font);
        const QFontMetrics meta_metrics(meta_font);

        const auto query = search_text_from(m_filter);
        const auto format = index.data(FileListModel::FormatRole).toString();
        const auto name = index.data(Qt::DisplayRole).toString();
        const auto highlight = selected
            ? QColor(255, 255, 255, 58)
            : QColor(accent.red(), accent.green(), accent.blue(), 55);

        if (m_compact) {
            const auto meta_width = std::min(
                std::max(meta_metrics.horizontalAdvance(format) + 12, 72),
                std::max(72, card.width() / 3)
            );
            const QRect meta_rect(
                card.right() - meta_width - 8,
                card.top(),
                meta_width,
                card.height()
            );
            const QRect text_rect(
                text_left,
                card.top(),
                std::max(0, meta_rect.left() - text_left - 8),
                card.height()
            );
            draw_highlighted_text(*painter, text_rect, name, title_font, text, highlight, query);
            draw_highlighted_text(
                *painter,
                meta_rect,
                format,
                meta_font,
                muted,
                highlight,
                query,
                Qt::AlignVCenter | Qt::AlignRight
            );
        } else {
            const auto content_height = title_metrics.height() + meta_metrics.height();
            const auto content_top = card.top() + std::max(0, (card.height() - content_height) / 2);
            const QRect text_rect(
                text_left,
                content_top,
                card.right() - text_left - 8,
                title_metrics.height()
            );
            const QRect meta_rect(
                text_left,
                text_rect.bottom() + 1,
                text_rect.width(),
                meta_metrics.height()
            );
            draw_highlighted_text(*painter, text_rect, name, title_font, text, highlight, query);
            draw_highlighted_text(*painter, meta_rect, format, meta_font, muted, highlight, query);
        }
        painter->restore();
}


EntryTreeDelegate::EntryTreeDelegate(QLineEdit* filter, QLineEdit* fallback_filter, QObject* parent)
    : QStyledItemDelegate(parent), m_filter(filter), m_fallback_filter(fallback_filter) {}

QSize EntryTreeDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(m_compact ? 21 : 26);
        return size;
    }

void EntryTreeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = {};

        const auto* widget = opt.widget;
        auto* style = widget == nullptr ? QApplication::style() : widget->style();
        const auto* view = qobject_cast<const QTreeView*>(widget);
        const auto selected = row_is_selected(view, index, option.state.testFlag(QStyle::State_Selected));
        const auto hovered = row_is_hovered(view, index, option.state.testFlag(QStyle::State_MouseOver));
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        if (!selected && !hovered) {
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const auto palette = option.palette;
        const auto accent = palette.color(QPalette::Highlight);
        const auto active = selected && widget != nullptr && widget->hasFocus();
        if (index.column() == 0 && widget != nullptr) {
            const QRect gutter(
                widget->rect().left(),
                option.rect.top(),
                std::max(0, option.rect.left() - widget->rect().left()),
                option.rect.height()
            );
            if (!gutter.isEmpty()) {
                painter->fillRect(gutter, palette.color(QPalette::Base));
            }
        }
        if (selected || hovered) {
            const auto fill = active
                ? accent
                : (selected
                    ? mixed_color(palette.color(QPalette::Base), accent, 0.18)
                    : palette.color(QPalette::AlternateBase));
            const auto outline = selected
                ? (active ? accent : mixed_color(palette.color(QPalette::Mid), accent, 0.32))
                : QColor(0, 0, 0, 0);
            painter->save();
            painter->setClipRect(option.rect);
            draw_selected_row(*painter, option.rect, fill, outline, active);
            painter->restore();
        }
        const auto text = active ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Text);
        const auto muted = active ? mixed_color(text, accent, 0.15) : palette.color(QPalette::Mid);
        const auto query = search_text_from(m_filter, m_fallback_filter);
        const auto highlight = active
            ? QColor(255, 255, 255, 58)
            : QColor(accent.red(), accent.green(), accent.blue(), 52);

        QRect text_rect = option.rect.adjusted(8, 0, -8, 0);
        if (index.column() == 0 && !m_compact) {
            const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            constexpr auto icon_size = 22;
            const QRect icon_rect(text_rect.left(), text_rect.top() + (text_rect.height() - icon_size) / 2, icon_size, icon_size);
            icon.paint(painter, icon_rect);
            text_rect.setLeft(icon_rect.right() + 8);
        }

        auto font = option.font;
        if (index.column() == 0) {
            font.setWeight(QFont::DemiBold);
        }
        const auto value = index.data(Qt::DisplayRole).toString();
        draw_highlighted_text(*painter, text_rect, value, font, index.column() == 0 ? text : muted, highlight, query);
        painter->restore();
    }
} // namespace cristudio
