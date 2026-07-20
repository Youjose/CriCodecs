#include "modules/audio/audio_encode_ui.hpp"

#include "modules/hca/hca_edit_ui.hpp"
#include "path_text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cristudio::modules::audio {
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

QString ensure_output_suffix(QString text, QString suffix) {
    text = text.trimmed();
    if (text.isEmpty()) {
        return suffix.isEmpty() ? QStringLiteral("output") : QStringLiteral("output%1").arg(suffix);
    }
    const auto dot = text.lastIndexOf(QLatin1Char('.'));
    const auto slash = std::max(text.lastIndexOf(QLatin1Char('/')), text.lastIndexOf(QLatin1Char('\\')));
    if (dot > slash) {
        text.truncate(dot);
    }
    return text + suffix;
}

QWidget* path_picker_row(QDialog& dialog, QLineEdit& edit, const QString& title, bool save_path, const QString& filter) {
    edit.setClearButtonEnabled(true);
    auto* row = new QWidget(&dialog);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(&edit, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse"), row);
    layout->addWidget(browse, 0);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, &edit, title, save_path, filter] {
        QString selected;
        if (save_path) {
            selected = QFileDialog::getSaveFileName(&dialog, title, edit.text(), filter);
        } else {
            selected = QFileDialog::getOpenFileName(&dialog, title, edit.text(), filter);
        }
        if (!selected.isEmpty()) {
            edit.setText(selected);
        }
    });
    return row;
}

} // namespace

std::expected<std::optional<EncodeConfig>, QString> choose_encode_config(
    QWidget* parent,
    QString title,
    DecryptionKeys keys,
    EncodeTarget preferred_target
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Encode from WAV"));
    dialog.resize(620, 420);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);

    auto* target_combo = new QComboBox(&dialog);
    target_combo->addItem(QStringLiteral("ADX ADPCM"), static_cast<int>(EncodeTarget::Adx));
    target_combo->addItem(QStringLiteral("AHX"), static_cast<int>(EncodeTarget::Adx));
    target_combo->addItem(QStringLiteral("HCA"), static_cast<int>(EncodeTarget::Hca));
    target_combo->setCurrentIndex(preferred_target == EncodeTarget::Hca ? 2 : 0);
    form->addRow(QStringLiteral("Target"), target_combo);

    auto* input_edit = new QLineEdit(&dialog);
    form->addRow(QStringLiteral("Input WAV"), path_picker_row(dialog, *input_edit, QStringLiteral("Choose WAV input"), false, QStringLiteral("WAV audio (*.wav);;All files (*)")));

    auto* output_edit = new QLineEdit(&dialog);
    output_edit->setText(safe_output_name(std::move(title) + QStringLiteral("_encoded"), preferred_target == EncodeTarget::Hca ? QStringLiteral(".hca") : QStringLiteral(".adx")));
    form->addRow(QStringLiteral("Output"), path_picker_row(dialog, *output_edit, QStringLiteral("Choose encoded output"), true, QStringLiteral("CRI audio (*.adx *.ahx *.hca);;All files (*)")));

    auto* adx_group = new QGroupBox(QStringLiteral("ADX/AHX Options"), &dialog);
    auto* adx_form = new QFormLayout(adx_group);
    adx_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(adx_group);

    auto* adx_mode_combo = new QComboBox(&dialog);
    adx_mode_combo->addItem(QStringLiteral("ADX mode 3"), 3);
    adx_mode_combo->addItem(QStringLiteral("ADX mode 2"), 2);
    adx_mode_combo->addItem(QStringLiteral("ADX mode 4"), 4);
    adx_mode_combo->addItem(QStringLiteral("AHX mode 0x10"), 0x10);
    adx_mode_combo->addItem(QStringLiteral("AHX mode 0x11"), 0x11);
    adx_form->addRow(QStringLiteral("Mode"), adx_mode_combo);

    auto* adx_highpass = new QSpinBox(&dialog);
    adx_highpass->setRange(0, 24000);
    adx_highpass->setValue(500);
    adx_highpass->setSuffix(QStringLiteral(" Hz"));
    adx_form->addRow(QStringLiteral("Highpass"), adx_highpass);

    auto* adx_encrypt = new QCheckBox(QStringLiteral("Use configured ADX type-8 key string"), &dialog);
    adx_encrypt->setEnabled(!keys.adx_type8_key.empty());
    adx_form->addRow(QStringLiteral("Encryption"), adx_encrypt);

    auto* adx_delete_after_loop = new QCheckBox(QStringLiteral("Trim samples after last loop end"), &dialog);
    adx_form->addRow(QStringLiteral("Loop policy"), adx_delete_after_loop);

    auto hca_options = modules::hca::create_encode_options_controls(
        &dialog,
        QStringLiteral("HCA Options"),
        QStringLiteral("Use configured CRI key"),
        QStringLiteral("WAV end")
    );
    layout->addWidget(hca_options.group);

    auto update_fields = [&] {
        const auto target = static_cast<EncodeTarget>(target_combo->currentData().toInt());
        const bool adx_target = target == EncodeTarget::Adx;
        const bool ahx_target = adx_target && target_combo->currentText() == QStringLiteral("AHX");
        const int current_mode = adx_mode_combo->currentData().toInt();
        if (ahx_target && current_mode != 0x10 && current_mode != 0x11) {
            adx_mode_combo->setCurrentIndex(adx_mode_combo->findData(0x10));
        } else if (!ahx_target && adx_target && (current_mode == 0x10 || current_mode == 0x11)) {
            adx_mode_combo->setCurrentIndex(adx_mode_combo->findData(3));
        }
        adx_group->setTitle(ahx_target ? QStringLiteral("AHX Options") : QStringLiteral("ADX Options"));
        adx_group->setVisible(adx_target);
        modules::hca::set_encode_options_enabled(hca_options, !adx_target, keys.has_cri_key);
        adx_mode_combo->setEnabled(adx_target);
        adx_highpass->setEnabled(adx_target);
        adx_encrypt->setEnabled(adx_target && !keys.adx_type8_key.empty());
        adx_delete_after_loop->setEnabled(adx_target && !ahx_target);
        output_edit->setText(ensure_output_suffix(
            output_edit->text(),
            !adx_target ? QStringLiteral(".hca") : (ahx_target ? QStringLiteral(".ahx") : QStringLiteral(".adx"))
        ));
    };
    QObject::connect(target_combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [update_fields](int) { update_fields(); });
    QObject::connect(adx_mode_combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [update_fields](int) { update_fields(); });
    QObject::connect(hca_options.loop_enabled, &QCheckBox::toggled, &dialog, update_fields);

    update_fields();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Encode"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<EncodeConfig>{};
    }
    if (input_edit->text().trimmed().isEmpty()) {
        return std::unexpected(QStringLiteral("Choose a WAV input path."));
    }
    if (output_edit->text().trimmed().isEmpty()) {
        return std::unexpected(QStringLiteral("Choose an output path."));
    }

    EncodeConfig config;
    config.target = static_cast<EncodeTarget>(target_combo->currentData().toInt());
    config.input_wav = path_from_qstring(input_edit->text());
    config.output_path = path_from_qstring(output_edit->text());
    config.keys = keys;
    config.adx_encoding_mode = static_cast<uint8_t>(adx_mode_combo->currentData().toInt());
    config.adx_highpass_frequency = static_cast<uint16_t>(adx_highpass->value());
    config.adx_encrypt_type8 = adx_encrypt->isChecked();
    config.adx_delete_after_loop = adx_delete_after_loop->isChecked();
    config.hca_config = modules::hca::encode_config_from_controls(hca_options, keys);
    config.hca_encrypt = modules::hca::encryption_checked(hca_options);

    if (config.target == EncodeTarget::Adx &&
        (config.adx_encoding_mode == 0x10 || config.adx_encoding_mode == 0x11) &&
        config.adx_delete_after_loop) {
        return std::unexpected(QStringLiteral("AHX encoding does not support loop metadata."));
    }
    return std::optional<EncodeConfig>(std::move(config));
}

} // namespace cristudio::modules::audio
