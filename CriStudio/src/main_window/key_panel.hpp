#pragma once

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

namespace cristudio {

QWidget* make_key_panel(
    QLabel*& label,
    QLineEdit*& input,
    QComboBox*& base,
    QToolButton*& apply,
    QWidget* parent
);
int key_base_value(const QComboBox* combo);

} // namespace cristudio
