#include "modules/adx/adx_edit_ui.hpp"

#include "modules/adx/adx_common.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstdint>
#include <limits>
#include <utility>

namespace cristudio::modules::adx {
namespace {

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    return label;
}

} // namespace

std::vector<TransformDetailRow> detail_rows(const cricodecs::adx::Adx& adx) {
    std::vector<TransformDetailRow> rows;
    const auto& header = adx.header();
    rows.push_back({QStringLiteral("Signature"), QStringLiteral("0x%1").arg(header.signature, 4, 16, QLatin1Char('0')).toUpper()});
    rows.push_back({QStringLiteral("Data offset"), QString::number(header.data_offset)});
    rows.push_back({QStringLiteral("Encoding mode"), QString::number(header.encoding_mode)});
    rows.push_back({QStringLiteral("Block size"), QString::number(header.block_size)});
    rows.push_back({QStringLiteral("Bit depth"), QString::number(header.bit_depth)});
    rows.push_back({QStringLiteral("Channels"), QString::number(header.channels)});
    rows.push_back({QStringLiteral("Sample rate"), QString::number(header.sample_rate)});
    rows.push_back({QStringLiteral("Sample count"), QString::number(header.sample_count)});
    rows.push_back({QStringLiteral("Highpass frequency"), QString::number(header.highpass_freq)});
    rows.push_back({QStringLiteral("Version"), QString::number(header.version)});
    rows.push_back({QStringLiteral("Flags"), QStringLiteral("0x%1").arg(header.flags, 2, 16, QLatin1Char('0')).toUpper()});
    rows.push_back({QStringLiteral("Encrypted"), adx.is_encrypted() ? QStringLiteral("yes") : QStringLiteral("no")});
    rows.push_back({QStringLiteral("AHX routed"), adx.is_ahx() ? QStringLiteral("yes") : QStringLiteral("no")});
    rows.push_back({QStringLiteral("Loop count"), QString::number(static_cast<qsizetype>(adx.loops().size()))});
    for (const auto& loop : adx.loops()) {
        rows.push_back({
            QStringLiteral("Loop %1").arg(loop.index),
            QStringLiteral("type %1, samples %2-%3, bytes %4-%5")
                .arg(loop.type)
                .arg(loop.start_sample)
                .arg(loop.end_sample)
                .arg(loop.start_byte)
                .arg(loop.end_byte)
        });
    }
    return rows;
}

std::expected<std::optional<cricodecs::adx::AdxEncodeConfig>, QString> choose_rebuild_config(
    QWidget* parent,
    const cricodecs::adx::Adx& adx,
    const DecryptionKeys& keys
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("ADX/AHX rebuild options"));
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(14);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    const auto& header = adx.header();
    const bool is_ahx = adx.is_ahx();
    auto* mode_combo = new QComboBox(&dialog);
    if (is_ahx) {
        mode_combo->addItem(QStringLiteral("AHX 0x10"), 0x10);
        mode_combo->addItem(QStringLiteral("AHX 0x11 (encoder delay)"), 0x11);
    } else {
        mode_combo->addItem(QStringLiteral("Mode 2 - fixed coefficient"), 2);
        mode_combo->addItem(QStringLiteral("Mode 3 - linear prediction"), 3);
        mode_combo->addItem(QStringLiteral("Mode 4 - exponential scale"), 4);
    }
    if (const auto index = mode_combo->findData(header.encoding_mode); index >= 0) {
        mode_combo->setCurrentIndex(index);
    }
    QComboBox* version_combo = nullptr;
    QSpinBox* highpass_spin = nullptr;
    QCheckBox* trim_check = nullptr;
    if (!is_ahx) {
        version_combo = new QComboBox(&dialog);
        version_combo->addItem(QStringLiteral("Version 3"), 3);
        version_combo->addItem(QStringLiteral("Version 4"), 4);
        version_combo->addItem(QStringLiteral("Version 5"), 5);
        if (const auto index = version_combo->findData(header.version); index >= 0) {
            version_combo->setCurrentIndex(index);
        }
        highpass_spin = new QSpinBox(&dialog);
        highpass_spin->setRange(0, 24000);
        highpass_spin->setValue(header.highpass_freq);
        highpass_spin->setSuffix(QStringLiteral(" Hz"));
        trim_check = new QCheckBox(QStringLiteral("Trim samples after first loop end"), &dialog);
    }
    auto* encrypt_combo = new QComboBox(&dialog);
    encrypt_combo->addItem(QStringLiteral("Plain"), 0);
    const bool can_write_type8 = keys.adx_mode == DecryptionKeys::AdxMode::Type8String
        || (is_ahx && keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet);
    const bool can_write_type9 = keys.adx_mode == DecryptionKeys::AdxMode::Type9Number
        || (is_ahx && keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet);
    if (can_write_type8) {
        encrypt_combo->addItem(QStringLiteral("Type 8 - use local key"), 8);
    }
    if (can_write_type9) {
        encrypt_combo->addItem(QStringLiteral("Type 9 - use local key"), 9);
    }
    if (const auto index = encrypt_combo->findData(header.flags == 8 || header.flags == 9 ? header.flags : 0); index >= 0) {
        encrypt_combo->setCurrentIndex(index);
    }
    form->addRow(is_ahx ? QStringLiteral("AHX profile") : QStringLiteral("Encoding mode"), mode_combo);
    if (!is_ahx) {
        form->addRow(QStringLiteral("Version"), version_combo);
        form->addRow(QStringLiteral("Highpass"), highpass_spin);
    }
    form->addRow(QStringLiteral("Encryption"), encrypt_combo);
    if (!is_ahx) {
        form->addRow(QStringLiteral("Loop policy"), trim_check);
    }
    layout->addLayout(form);

    auto* note = dim_label(is_ahx
        ? QStringLiteral("AHX rebuild uses MPEG Layer II profiles. Encrypted output is offered only when this tab has a compatible local key.")
        : QStringLiteral("ADX rebuild decodes with the session key, then re-encodes. Encrypted output is offered only for a compatible local key type."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Rebuild Session"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<cricodecs::adx::AdxEncodeConfig>{};
    }
    if (!has_compatible_key(adx, keys)) {
        return std::unexpected(adx.is_ahx()
            ? QStringLiteral("This encrypted AHX input needs a compatible local key before it can be decoded and rebuilt.")
            : QStringLiteral("This encrypted ADX input needs a compatible local key before it can be decoded and rebuilt."));
    }

    cricodecs::adx::AdxEncodeConfig config;
    config.encoding_mode = static_cast<uint8_t>(mode_combo->currentData().toUInt());
    config.version = is_ahx ? header.version : static_cast<uint8_t>(version_combo->currentData().toUInt());
    config.highpass_freq = is_ahx ? header.highpass_freq : static_cast<uint16_t>(highpass_spin->value());
    config.encryption_type = static_cast<uint8_t>(encrypt_combo->currentData().toUInt());
    config.delete_samples_after_loop_end = !is_ahx && trim_check->isChecked();
    return std::optional<cricodecs::adx::AdxEncodeConfig>(config);
}

} // namespace cristudio::modules::adx
