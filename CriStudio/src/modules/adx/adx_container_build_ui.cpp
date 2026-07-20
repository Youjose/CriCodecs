#include "modules/adx/adx_container_build_ui.hpp"

#include "path_text.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

namespace cristudio::modules::adx {
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

QLabel* value_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
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
            QStringLiteral("Choose build output"),
            edit.text(),
            QStringLiteral("CRI ADX containers (*.aax *.aix);;All files (*)")
        );
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    return row;
}

std::expected<std::vector<std::vector<std::filesystem::path>>, QString> parse_sources(
    const QPlainTextEdit& sources_edit,
    bool aix_target
) {
    std::vector<std::vector<std::filesystem::path>> segments;
    const auto lines = sources_edit.toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto& raw_line : lines) {
        const auto line = raw_line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto parts = line.split(aix_target ? QLatin1Char(';') : QLatin1Char('\n'), Qt::SkipEmptyParts);
        std::vector<std::filesystem::path> segment;
        segment.reserve(static_cast<size_t>(parts.size()));
        for (const auto& raw_part : parts) {
            const auto part = raw_part.trimmed();
            if (!part.isEmpty()) {
                segment.push_back(path_from_qstring(part));
            }
        }
        if (!segment.empty()) {
            segments.push_back(std::move(segment));
        }
    }
    if (segments.empty()) {
        return std::unexpected(QStringLiteral("Choose at least one ADX source."));
    }
    if (!aix_target) {
        for (const auto& segment : segments) {
            if (segment.size() != 1) {
                return std::unexpected(QStringLiteral("AAX expects one ADX file per line."));
            }
        }
    }
    for (const auto& segment : segments) {
        for (const auto& path : segment) {
            if (!std::filesystem::exists(path)) {
                return std::unexpected(QStringLiteral("ADX source does not exist: %1").arg(path_to_qstring(path)));
            }
        }
    }
    return segments;
}

} // namespace

std::expected<std::optional<AdxContainerBuildConfig>, QString> choose_container_build_config(
    QWidget* parent,
    QString title,
    bool aix_target
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(aix_target ? QStringLiteral("AIX ADX Builder") : QStringLiteral("AAX ADX Builder"));
    dialog.setMinimumWidth(520);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->addLayout(form);

    auto* target_label = value_label(aix_target ? QStringLiteral("AIX layered ADX") : QStringLiteral("AAX segmented ADX"), &dialog);
    target_label->setMinimumWidth(180);
    form->addRow(QStringLiteral("Target"), target_label);

    auto* sources_edit = new QPlainTextEdit(&dialog);
    sources_edit->setMinimumWidth(390);
    sources_edit->setMinimumHeight(110);
    sources_edit->setPlaceholderText(aix_target
        ? QStringLiteral("segment0_layer0.adx; segment0_layer1.adx")
        : QStringLiteral("segment0.adx\nsegment1.adx"));
    form->addRow(QStringLiteral("Segments"), sources_edit);

    auto* source_buttons = new QWidget(&dialog);
    auto* source_button_layout = new QHBoxLayout(source_buttons);
    source_button_layout->setContentsMargins(0, 0, 0, 0);
    source_button_layout->setSpacing(6);
    auto* add_sources_button = new QPushButton(aix_target ? QStringLiteral("Add Segment") : QStringLiteral("Add ADX Files"), source_buttons);
    auto* clear_sources_button = new QPushButton(QStringLiteral("Clear"), source_buttons);
    source_button_layout->addWidget(add_sources_button, 0);
    source_button_layout->addWidget(clear_sources_button, 0);
    source_button_layout->addStretch(1);
    form->addRow(QString{}, source_buttons);

    QObject::connect(add_sources_button, &QPushButton::clicked, &dialog, [&dialog, sources_edit, aix_target] {
        const auto selected = QFileDialog::getOpenFileNames(
            &dialog,
            aix_target ? QStringLiteral("Choose ADX layer files for one AIX segment") : QStringLiteral("Choose AAX ADX segment files"),
            QString{},
            QStringLiteral("ADX audio (*.adx *.ahx);;All files (*)")
        );
        if (selected.isEmpty()) {
            return;
        }
        auto text = sources_edit->toPlainText().trimmed();
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += aix_target ? selected.join(QStringLiteral("; ")) : selected.join(QLatin1Char('\n'));
        sources_edit->setPlainText(text);
    });
    QObject::connect(clear_sources_button, &QPushButton::clicked, sources_edit, &QPlainTextEdit::clear);

    auto* loop_last_check = new QCheckBox(QStringLiteral("Mark last AAX segment as loop"), &dialog);
    loop_last_check->setVisible(!aix_target);
    form->addRow(QString{}, loop_last_check);

    auto* output_edit = new QLineEdit(&dialog);
    output_edit->setText(safe_output_name(build_output_base_name(std::move(title)), aix_target ? QStringLiteral(".aix") : QStringLiteral(".aax")));
    form->addRow(QStringLiteral("Output"), save_picker_row(dialog, *output_edit));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Build"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<AdxContainerBuildConfig>{};
    }

    auto segments = parse_sources(*sources_edit, aix_target);
    if (!segments) {
        return std::unexpected(segments.error());
    }

    auto output_path = path_from_qstring(output_edit->text().trimmed());
    if (output_path.empty()) {
        return std::unexpected(QStringLiteral("Choose an output path."));
    }

    if (aix_target) {
        return std::optional<AdxContainerBuildConfig>(modules::aix::BuildConfig{
            .segments = std::move(*segments),
            .output_path = output_path
        });
    }

    modules::aax::BuildConfig config;
    config.output_path = output_path;
    config.mark_last_segment_as_loop = loop_last_check->isChecked();
    config.segments.reserve(segments->size());
    for (auto& segment : *segments) {
        config.segments.push_back(std::move(segment.front()));
    }
    return std::optional<AdxContainerBuildConfig>(std::move(config));
}

} // namespace cristudio::modules::adx
