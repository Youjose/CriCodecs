#pragma once

#include <QLineEdit>
#include <QString>
#include <QValidator>

#include <cstdint>
#include <expected>
#include <limits>
#include <utility>

class QWidget;

namespace cristudio::modules {

class UnsignedIntegerValidator final : public QValidator {
public:
    explicit UnsignedIntegerValidator(uint64_t minimum, uint64_t maximum, QObject* parent = nullptr)
        : QValidator(parent), m_minimum(minimum), m_maximum(maximum) {}

    State validate(QString& input, int&) const override {
        if (input.isEmpty()) {
            return Intermediate;
        }
        for (const auto character : input) {
            if (!character.isDigit()) {
                return Invalid;
            }
        }
        bool ok = false;
        const auto value = input.toULongLong(&ok, 10);
        if (!ok || value > m_maximum) {
            return Invalid;
        }
        return value >= m_minimum ? Acceptable : Intermediate;
    }

private:
    uint64_t m_minimum;
    uint64_t m_maximum;
};

inline QLineEdit* make_unsigned_integer_edit(
    uint64_t value,
    uint64_t minimum,
    uint64_t maximum,
    QWidget* parent,
    QString accessible_name = {}
) {
    auto* edit = new QLineEdit(QString::number(static_cast<qulonglong>(value)), parent);
    edit->setValidator(new UnsignedIntegerValidator(minimum, maximum, edit));
    if (!accessible_name.isEmpty()) {
        edit->setAccessibleName(accessible_name);
    }
    edit->setToolTip(QStringLiteral("Range: %1 to %2")
        .arg(static_cast<qulonglong>(minimum))
        .arg(static_cast<qulonglong>(maximum)));
    return edit;
}

inline std::expected<uint64_t, QString> unsigned_integer_value(
    const QLineEdit* edit,
    uint64_t minimum,
    uint64_t maximum,
    QString field_name
) {
    bool ok = false;
    const auto value = edit->text().trimmed().toULongLong(&ok, 10);
    if (!ok || value < minimum || value > maximum) {
        return std::unexpected(QStringLiteral("%1 must be between %2 and %3.")
            .arg(std::move(field_name))
            .arg(static_cast<qulonglong>(minimum))
            .arg(static_cast<qulonglong>(maximum)));
    }
    return static_cast<uint64_t>(value);
}

} // namespace cristudio::modules
