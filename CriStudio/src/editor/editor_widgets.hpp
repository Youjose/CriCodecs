#pragma once

#include <QAbstractButton>
#include <QString>

class QLabel;
class QPaintEvent;
class QSize;
class QTabWidget;
class QWidget;

namespace cristudio {

class ToggleSwitch final : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

[[nodiscard]] QLabel* dim_label(QString text, QWidget* parent);
[[nodiscard]] QLabel* value_label(QString text, QWidget* parent);
void add_editor_start_tab(QTabWidget* tabs);
void remove_editor_tab(QTabWidget* tabs, QWidget* widget);

} // namespace cristudio
