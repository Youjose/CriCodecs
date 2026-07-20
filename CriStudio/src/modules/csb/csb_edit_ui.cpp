#include "modules/csb/csb_edit_ui.hpp"

#include "path_text.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace cristudio::modules::csb {
namespace {

QString safe_output_name(QString name, QString fallback_suffix) {
    name = name.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("editor-output");
    }
    for (auto& ch : name) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':') ||
            ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')) {
            ch = QLatin1Char('_');
        }
    }
    if (!fallback_suffix.isEmpty() && !name.endsWith(fallback_suffix, Qt::CaseInsensitive)) {
        name += fallback_suffix;
    }
    return name;
}

QString build_output_base_name(const QString& title) {
    auto base = title.trimmed();
    const auto dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        base.truncate(dot);
    }
    return base.isEmpty() ? QStringLiteral("build") : base;
}

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    return label;
}

QLabel* value_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QWidget* directory_picker_row(QDialog& dialog, QLineEdit& edit) {
    edit.setClearButtonEnabled(true);
    auto* row = new QWidget(&dialog);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(&edit, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse"), row);
    layout->addWidget(browse, 0);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, &edit] {
        const auto selected = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Choose extracted CSB source folder"), edit.text());
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    return row;
}

QWidget* save_picker_row(QDialog& dialog, QLineEdit& edit) {
    edit.setClearButtonEnabled(true);
    auto* row = new QWidget(&dialog);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(&edit, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse"), row);
    layout->addWidget(browse, 0);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, &edit] {
        const auto selected = QFileDialog::getSaveFileName(
            &dialog,
            QStringLiteral("Choose CSB build output"),
            edit.text(),
            QStringLiteral("CRI CSB (*.csb);;All files (*)")
        );
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    return row;
}

} // namespace

std::expected<std::optional<DirectoryBuildConfig>, QString> choose_directory_build_config(
    QWidget* parent,
    QString title
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("CSB Folder Builder"));
    dialog.setMinimumWidth(540);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->addLayout(form);

    auto* target_label = value_label(QStringLiteral("CSB directory rebuild"), &dialog);
    target_label->setMinimumWidth(200);
    form->addRow(QStringLiteral("Target"), target_label);

    auto* source_edit = new QLineEdit(&dialog);
    form->addRow(QStringLiteral("Source folder"), directory_picker_row(dialog, *source_edit));

    auto* output_edit = new QLineEdit(&dialog);
    output_edit->setText(safe_output_name(build_output_base_name(std::move(title)), QStringLiteral(".csb")));
    form->addRow(QStringLiteral("Output"), save_picker_row(dialog, *output_edit));

    auto* details = dim_label(
        QStringLiteral("The native builder recurses through the folder, uses relative paths as stream names, and supports the payload types accepted by CsbContainer."),
        &dialog
    );
    details->setWordWrap(true);
    form->addRow(QString{}, details);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Build"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<DirectoryBuildConfig>{};
    }

    DirectoryBuildConfig config;
    config.input_dir = path_from_qstring(source_edit->text().trimmed());
    config.output_path = path_from_qstring(output_edit->text().trimmed());
    if (config.input_dir.empty()) {
        return std::unexpected(QStringLiteral("Choose a CSB source folder."));
    }
    if (config.output_path.empty()) {
        return std::unexpected(QStringLiteral("Choose an output path."));
    }
    return std::optional<DirectoryBuildConfig>(std::move(config));
}

} // namespace cristudio::modules::csb
