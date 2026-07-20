#pragma once

#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include "hca_codec.hpp"

#include <QString>

#include <expected>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace cristudio::modules::hca {

struct EncodeOptionsControls {
    QGroupBox* group = nullptr;
    QComboBox* version = nullptr;
    QComboBox* quality = nullptr;
    QSpinBox* bitrate = nullptr;
    QCheckBox* ms_stereo = nullptr;
    QCheckBox* encrypt = nullptr;
    QCheckBox* loop_enabled = nullptr;
    QLineEdit* loop_start = nullptr;
    QLineEdit* loop_end = nullptr;
};

[[nodiscard]] EncodeOptionsControls create_encode_options_controls(
    QWidget* parent,
    const QString& title,
    const QString& encryption_label,
    const QString& loop_end_special_text
);

void set_encode_options_enabled(const EncodeOptionsControls& controls, bool enabled, bool has_cri_key);
void set_encode_options_from_hca(const EncodeOptionsControls& controls, const cricodecs::hca::Hca& hca, bool has_cri_key);

[[nodiscard]] cricodecs::hca::HcaEncodeConfig encode_config_from_controls(
    const EncodeOptionsControls& controls,
    const DecryptionKeys& keys,
    uint32_t sample_rate = 0,
    uint16_t channel_count = 0
);

[[nodiscard]] bool encryption_checked(const EncodeOptionsControls& controls);

[[nodiscard]] std::expected<std::optional<cricodecs::hca::HcaEncodeConfig>, QString> choose_rebuild_config(
    QWidget* parent,
    const cricodecs::hca::Hca& hca,
    const DecryptionKeys& keys
);

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::hca::Hca& hca);

} // namespace cristudio::modules::hca
