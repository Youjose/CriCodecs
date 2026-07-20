#include "modules/hca/hca_edit_ui.hpp"

#include "modules/hca/hca_common.hpp"
#include "modules/ui_value_helpers.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace cristudio::modules::hca {
namespace {

void add_version_item(QComboBox& combo, const QString& label, uint16_t version) {
    combo.addItem(label, version);
}

void set_combo_value(QComboBox& combo, uint16_t value) {
    if (const auto index = combo.findData(value); index >= 0) {
        combo.setCurrentIndex(index);
        return;
    }
    combo.addItem(QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper(), value);
    combo.setCurrentIndex(combo.count() - 1);
}

void sync_loop_controls(const EncodeOptionsControls& controls) {
    const bool enabled = controls.loop_enabled->isEnabled() && controls.loop_enabled->isChecked();
    controls.loop_start->setEnabled(enabled);
    controls.loop_end->setEnabled(enabled);
}

QLabel* dim_label(QString text, QWidget* parent) {
    auto* label = new QLabel(std::move(text), parent);
    label->setObjectName(QStringLiteral("DimLabel"));
    return label;
}

} // namespace

EncodeOptionsControls create_encode_options_controls(
    QWidget* parent,
    const QString& title,
    const QString& encryption_label,
    const QString& loop_end_special_text
) {
    EncodeOptionsControls controls;
    controls.group = new QGroupBox(title, parent);
    auto* form = new QFormLayout(controls.group);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    controls.version = new QComboBox(parent);
    add_version_item(*controls.version, QStringLiteral("2.00 comp"), cricodecs::hca::HCA_VERSION_V200);
    add_version_item(*controls.version, QStringLiteral("3.00 comp"), cricodecs::hca::HCA_VERSION_V300);
    add_version_item(*controls.version, QStringLiteral("1.02 dec"), cricodecs::hca::HCA_VERSION_V102);
    add_version_item(*controls.version, QStringLiteral("1.03 dec"), cricodecs::hca::HCA_VERSION_V103);
    form->addRow(QStringLiteral("Version"), controls.version);

    controls.quality = new QComboBox(parent);
    controls.quality->addItem(QStringLiteral("Highest"), static_cast<int>(cricodecs::hca::HcaQuality::Highest));
    controls.quality->addItem(QStringLiteral("High"), static_cast<int>(cricodecs::hca::HcaQuality::High));
    controls.quality->addItem(QStringLiteral("Middle"), static_cast<int>(cricodecs::hca::HcaQuality::Middle));
    controls.quality->addItem(QStringLiteral("Low"), static_cast<int>(cricodecs::hca::HcaQuality::Low));
    controls.quality->addItem(QStringLiteral("Lowest"), static_cast<int>(cricodecs::hca::HcaQuality::Lowest));
    controls.quality->setCurrentIndex(1);
    form->addRow(QStringLiteral("Quality"), controls.quality);

    controls.bitrate = new QSpinBox(parent);
    controls.bitrate->setRange(0, std::numeric_limits<int>::max());
    controls.bitrate->setSpecialValueText(QStringLiteral("auto"));
    controls.bitrate->setSuffix(QStringLiteral(" bps"));
    form->addRow(QStringLiteral("Bitrate"), controls.bitrate);

    controls.ms_stereo = new QCheckBox(QStringLiteral("Use M/S stereo where supported"), parent);
    form->addRow(QStringLiteral("Stereo"), controls.ms_stereo);

    controls.encrypt = new QCheckBox(encryption_label, parent);
    form->addRow(QStringLiteral("Encryption"), controls.encrypt);

    controls.loop_enabled = new QCheckBox(QStringLiteral("Write HCA loop chunk"), parent);
    form->addRow(QStringLiteral("Loop"), controls.loop_enabled);

    controls.loop_start = make_unsigned_integer_edit(
        0, 0, std::numeric_limits<uint32_t>::max(), parent, QStringLiteral("Loop start in samples"));
    form->addRow(QStringLiteral("Loop start (samples)"), controls.loop_start);

    controls.loop_end = make_unsigned_integer_edit(
        0, 0, std::numeric_limits<uint32_t>::max(), parent, QStringLiteral("Loop end in samples"));
    if (!loop_end_special_text.isEmpty()) {
        controls.loop_end->setToolTip(QStringLiteral("0 means %1. Range: 0 to %2 samples.")
            .arg(loop_end_special_text)
            .arg(std::numeric_limits<uint32_t>::max()));
    }
    form->addRow(QStringLiteral("Loop end (samples)"), controls.loop_end);

    QObject::connect(controls.loop_enabled, &QCheckBox::toggled, controls.group, [controls] {
        sync_loop_controls(controls);
    });
    set_encode_options_enabled(controls, true, true);
    return controls;
}

void set_encode_options_enabled(const EncodeOptionsControls& controls, bool enabled, bool has_cri_key) {
    controls.group->setVisible(enabled);
    controls.version->setEnabled(enabled);
    controls.quality->setEnabled(enabled);
    controls.bitrate->setEnabled(enabled);
    controls.ms_stereo->setEnabled(enabled);
    controls.encrypt->setEnabled(enabled && has_cri_key);
    controls.loop_enabled->setEnabled(enabled);
    sync_loop_controls(controls);
}

void set_encode_options_from_hca(const EncodeOptionsControls& controls, const cricodecs::hca::Hca& hca, bool has_cri_key) {
    const auto& header = hca.header();
    set_combo_value(*controls.version, header.file.version);
    controls.quality->setCurrentIndex(1);
    controls.bitrate->setValue(0);
    controls.ms_stereo->setChecked(header.codec.uses_ms_stereo());
    controls.encrypt->setEnabled(has_cri_key);
    controls.encrypt->setChecked(header.cipher.encrypted());
    controls.loop_enabled->setChecked(header.loop.enabled());
    if (header.loop.enabled()) {
        controls.loop_start->setText(QString::number(static_cast<qulonglong>(
            header.loop.start_frame * cricodecs::hca::HCA_SAMPLES_PER_FRAME + header.loop.start_delay)));
        controls.loop_end->setText(QString::number(static_cast<qulonglong>(
            header.loop.end_frame * cricodecs::hca::HCA_SAMPLES_PER_FRAME +
                cricodecs::hca::HCA_SAMPLES_PER_FRAME - header.loop.end_padding)));
    }
    sync_loop_controls(controls);
}

cricodecs::hca::HcaEncodeConfig encode_config_from_controls(
    const EncodeOptionsControls& controls,
    const DecryptionKeys& keys,
    uint32_t sample_rate,
    uint16_t channel_count
) {
    cricodecs::hca::HcaEncodeConfig config;
    config.sample_rate = sample_rate;
    config.channel_count = channel_count;
    config.version = static_cast<uint16_t>(controls.version->currentData().toUInt());
    config.quality = static_cast<cricodecs::hca::HcaQuality>(controls.quality->currentData().toInt());
    config.bitrate = static_cast<uint32_t>(controls.bitrate->value());
    config.ms_stereo = controls.ms_stereo->isChecked();
    config.loop_enabled = controls.loop_enabled->isChecked();
    config.loop_start = controls.loop_start->text().toUInt();
    config.loop_end = controls.loop_end->text().toUInt();
    if (controls.encrypt->isChecked()) {
        config.keycode = keys.has_cri_key ? keys.cri_key : 0;
        config.subkey = keys.hca_subkey;
    }
    return config;
}

bool encryption_checked(const EncodeOptionsControls& controls) {
    return controls.encrypt->isChecked();
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::hca::Hca& hca) {
    std::vector<TransformDetailRow> rows;
    const auto& header = hca.header();
    rows.push_back({QStringLiteral("Version"), QStringLiteral("0x%1").arg(header.file.version, 4, 16, QLatin1Char('0')).toUpper()});
    rows.push_back({QStringLiteral("Header size"), QString::number(header.file.header_size)});
    rows.push_back({QStringLiteral("Channels"), QString::number(header.fmt.channel_count)});
    rows.push_back({QStringLiteral("Sample rate"), QString::number(header.fmt.sample_rate)});
    rows.push_back({QStringLiteral("Frame count"), QString::number(header.fmt.frame_count)});
    rows.push_back({QStringLiteral("Sample count"), QString::number(header.sample_count())});
    rows.push_back({QStringLiteral("Encoder delay"), QString::number(header.fmt.encoder_delay)});
    rows.push_back({QStringLiteral("Encoder padding"), QString::number(header.fmt.encoder_padding)});
    rows.push_back({QStringLiteral("Codec type"), QString::fromStdString(codec_type_name(header.codec.type()))});
    rows.push_back({QStringLiteral("Frame size"), QString::number(header.codec.frame_size)});
    rows.push_back({QStringLiteral("Resolution"), QStringLiteral("%1-%2").arg(header.codec.min_resolution).arg(header.codec.max_resolution)});
    rows.push_back({QStringLiteral("Track count"), QString::number(header.codec.track_count)});
    rows.push_back({QStringLiteral("Channel config"), QString::number(header.codec.channel_config)});
    rows.push_back({
        QStringLiteral("Bands"),
        QStringLiteral("total %1, base %2, stereo %3, hfr %4 x %5")
            .arg(header.codec.total_band_count)
            .arg(header.codec.base_band_count)
            .arg(header.codec.stereo_band_count)
            .arg(header.codec.bands_per_hfr_group)
            .arg(header.codec.hfr_group_count)
    });
    rows.push_back({QStringLiteral("MS stereo"), header.codec.uses_ms_stereo() ? QStringLiteral("yes") : QStringLiteral("no")});
    rows.push_back({
        QStringLiteral("VBR"),
        header.vbr.enabled()
            ? QStringLiteral("max frame %1, noise %2").arg(header.vbr.max_frame_size).arg(header.vbr.noise_level)
            : QStringLiteral("no")
    });
    rows.push_back({
        QStringLiteral("ATH"),
        QStringLiteral("type %1, curve %2").arg(header.ath.type).arg(header.ath.uses_curve() ? QStringLiteral("yes") : QStringLiteral("no"))
    });
    rows.push_back({
        QStringLiteral("Cipher"),
        QStringLiteral("type %1, encrypted %2").arg(header.cipher.type).arg(header.cipher.encrypted() ? QStringLiteral("yes") : QStringLiteral("no"))
    });
    rows.push_back({
        QStringLiteral("Loop"),
        header.loop.enabled()
            ? QStringLiteral("frames %1-%2, delay %3, padding %4")
                .arg(header.loop.start_frame)
                .arg(header.loop.end_frame)
                .arg(header.loop.start_delay)
                .arg(header.loop.end_padding)
            : QStringLiteral("no")
    });
    rows.push_back({QStringLiteral("RVA volume"), QString::number(header.rva.volume, 'g', 9)});
    rows.push_back({QStringLiteral("Comment length"), QString::number(header.comment.length)});
    return rows;
}

std::expected<std::optional<cricodecs::hca::HcaEncodeConfig>, QString> choose_rebuild_config(
    QWidget* parent,
    const cricodecs::hca::Hca& hca,
    const DecryptionKeys& keys
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("HCA rebuild options"));
    auto* layout = new QVBoxLayout(&dialog);

    const auto& header = hca.header();
    auto hca_options = create_encode_options_controls(
        &dialog,
        QStringLiteral("HCA Options"),
        QStringLiteral("Encrypt with configured CRI key"),
        QStringLiteral("source end")
    );
    set_encode_options_from_hca(hca_options, hca, keys.has_cri_key);
    layout->addWidget(hca_options.group);

    auto* note = dim_label(QStringLiteral("Rebuild decodes the current HCA with the session key, then re-encodes PCM with these options."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Rebuild Session"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<cricodecs::hca::HcaEncodeConfig>{};
    }

    return std::optional<cricodecs::hca::HcaEncodeConfig>(encode_config_from_controls(
        hca_options,
        keys,
        header.fmt.sample_rate,
        header.fmt.channel_count
    ));
}

} // namespace cristudio::modules::hca
