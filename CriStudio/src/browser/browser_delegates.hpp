#pragma once

#include <QStyledItemDelegate>

class QLineEdit;
class QModelIndex;
class QObject;
class QPainter;
class QSize;
class QStyleOptionViewItem;

namespace cristudio {

class LoadedFileDelegate final : public QStyledItemDelegate {
public:
    explicit LoadedFileDelegate(QLineEdit* filter, QObject* parent = nullptr);
    void set_compact(bool compact) noexcept { m_compact = compact; }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    QLineEdit* m_filter = nullptr;
    bool m_compact = false;
};

class EntryTreeDelegate final : public QStyledItemDelegate {
public:
    explicit EntryTreeDelegate(QLineEdit* filter, QLineEdit* fallback_filter = nullptr, QObject* parent = nullptr);
    void set_compact(bool compact) noexcept { m_compact = compact; }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    QLineEdit* m_filter = nullptr;
    QLineEdit* m_fallback_filter = nullptr;
    bool m_compact = false;
};

} // namespace cristudio
