#include "modules/awb/awb_edit_ui.hpp"

#include "editor/table_item_helpers.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/ui_value_helpers.hpp"
#include "path_text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace cristudio::modules::awb {
namespace {

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void bind_valid_inputs(QPushButton* accept, std::initializer_list<QLineEdit*> edits) {
    const auto refresh = [accept, edits] {
        accept->setEnabled(std::ranges::all_of(edits, [](const QLineEdit* edit) {
            return edit->hasAcceptableInput();
        }));
    };
    for (auto* edit : edits) {
        QObject::connect(edit, &QLineEdit::textChanged, accept, [refresh](const QString&) { refresh(); });
    }
    refresh();
}

} // namespace

void populate_editor_archive_table(
    QTableWidget* table,
    const cricodecs::awb::AwbContainer& awb,
    const DecryptionKeys& keys
) {
    table->clear();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Index"),
        QStringLiteral("Wave ID"),
        QStringLiteral("Offset"),
        QStringLiteral("Size"),
        QStringLiteral("Suggested Path"),
        QStringLiteral("AAC State")
    });
    table->setRowCount(static_cast<int>(awb.file_count()));
    for (uint32_t index = 0; index < awb.file_count(); ++index) {
        const auto& entry = awb.entry(index);
        const auto row = static_cast<int>(index);
        set_table_item(table, row, 0, QString::number(index), false);
        set_table_item(table, row, 1, QString::number(static_cast<qulonglong>(entry.wave_id)), true);
        set_table_item(table, row, 2, QStringLiteral("0x%1").arg(entry.offset, 0, 16).toUpper(), false);
        set_table_item(table, row, 3, QString::number(static_cast<qulonglong>(entry.size)), false);
        auto suggested = awb.suggested_path(index);
        set_table_item(table, row, 4, suggested ? path_to_qstring(*suggested) : utf8_to_qstring(suggested.error()), false);
        set_table_item(table, row, 5, aac_probe_text(awb, index, keys), false);
    }
}

std::optional<uint64_t> choose_wave_id(
    QWidget* parent,
    const cricodecs::awb::AwbContainer& awb,
    uint32_t index
) {
    if (index >= awb.file_count()) {
        return std::nullopt;
    }

    const auto current = awb.entry(index).wave_id;
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Set AWB wave ID"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* wave_id_edit = make_unsigned_integer_edit(
        current, 0, std::numeric_limits<uint64_t>::max(), &dialog, QStringLiteral("Wave ID"));
    form->addRow(QStringLiteral("Wave ID"), wave_id_edit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    bind_valid_inputs(buttons->button(QDialogButtonBox::Ok), {wave_id_edit});
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    auto wave_id = unsigned_integer_value(
        wave_id_edit, 0, std::numeric_limits<uint64_t>::max(), QStringLiteral("Wave ID"));
    return wave_id ? std::optional<uint64_t>(*wave_id) : std::nullopt;
}

std::optional<BatchWaveIdOptions> choose_batch_wave_ids(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Batch AWB wave IDs"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* start_edit = make_unsigned_integer_edit(
        0, 0, std::numeric_limits<uint64_t>::max(), &dialog, QStringLiteral("Start wave ID"));
    auto* step_edit = make_unsigned_integer_edit(
        1, 1, std::numeric_limits<uint64_t>::max(), &dialog, QStringLiteral("Wave ID step"));
    form->addRow(QStringLiteral("Start wave ID"), start_edit);
    form->addRow(QStringLiteral("Step"), step_edit);
    layout->addLayout(form);

    auto* note = dim_label(QStringLiteral("Wave IDs are assigned by current entry order. Payload bytes are unchanged."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Assign"));
    bind_valid_inputs(buttons->button(QDialogButtonBox::Ok), {start_edit, step_edit});
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    const auto start = unsigned_integer_value(
        start_edit, 0, std::numeric_limits<uint64_t>::max(), QStringLiteral("Start wave ID"));
    const auto step = unsigned_integer_value(
        step_edit, 1, std::numeric_limits<uint64_t>::max(), QStringLiteral("Step"));
    if (!start || !step) {
        return std::nullopt;
    }
    return BatchWaveIdOptions{.start = *start, .step = *step};
}

std::optional<BuildOptions> choose_build_options(QWidget* parent, const cricodecs::awb::AwbContainer& awb) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("AWB/AFS2 options"));
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* version_combo = new QComboBox(&dialog);
    version_combo->setEditable(true);
    version_combo->addItems({QStringLiteral("1"), QStringLiteral("2")});
    version_combo->lineEdit()->setValidator(new QIntValidator(0, std::numeric_limits<uint8_t>::max(), version_combo));
    version_combo->setCurrentText(QString::number(awb.version()));

    auto* alignment_spin = new QSpinBox(&dialog);
    alignment_spin->setRange(1, std::numeric_limits<uint16_t>::max());
    alignment_spin->setValue(awb.alignment());
    alignment_spin->setSuffix(QStringLiteral(" bytes"));

    auto* subkey_spin = new QSpinBox(&dialog);
    subkey_spin->setRange(0, std::numeric_limits<uint16_t>::max());
    subkey_spin->setValue(awb.subkey());

    auto* id_size_combo = new QComboBox(&dialog);
    for (const int size : {1, 2, 4, 8}) {
        id_size_combo->addItem(QStringLiteral("%1 %2").arg(size).arg(size == 1 ? QStringLiteral("byte") : QStringLiteral("bytes")), size);
    }
    if (const int combo_index = id_size_combo->findData(awb.id_size()); combo_index >= 0) {
        id_size_combo->setCurrentIndex(combo_index);
    }

    auto* offset_size_combo = new QComboBox(&dialog);
    for (const int size : {2, 4, 8}) {
        offset_size_combo->addItem(QStringLiteral("%1 bytes").arg(size), size);
    }
    if (const int combo_index = offset_size_combo->findData(awb.offset_size()); combo_index >= 0) {
        offset_size_combo->setCurrentIndex(combo_index);
    }

    form->addRow(QStringLiteral("Files"), new QLabel(QString::number(awb.file_count()), &dialog));
    form->addRow(QStringLiteral("Version"), version_combo);
    form->addRow(QStringLiteral("Alignment"), alignment_spin);
    form->addRow(QStringLiteral("Subkey"), subkey_spin);
    form->addRow(QStringLiteral("ID size"), id_size_combo);
    form->addRow(QStringLiteral("Offset size"), offset_size_combo);
    layout->addLayout(form);

    auto* note = dim_label(
        QStringLiteral("AWB/AFS2 rebuilds keep entry payloads independent from the browser copy. Size controls are validated by the native AWB builder."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return BuildOptions{
        .version = static_cast<uint8_t>(version_combo->currentText().toUInt()),
        .alignment = static_cast<uint16_t>(alignment_spin->value()),
        .subkey = static_cast<uint16_t>(subkey_spin->value()),
        .id_size = static_cast<uint8_t>(id_size_combo->currentData().toUInt()),
        .offset_size = static_cast<uint8_t>(offset_size_combo->currentData().toUInt())
    };
}

} // namespace cristudio::modules::awb
