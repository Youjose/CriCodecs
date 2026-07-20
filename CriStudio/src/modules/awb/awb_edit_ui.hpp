#pragma once

#include "awb_container.hpp"

#include <cstdint>
#include <optional>

class QWidget;
class QTableWidget;

namespace cristudio {
struct DecryptionKeys;
}

namespace cristudio::modules::awb {

struct BatchWaveIdOptions {
    uint64_t start = 0;
    uint64_t step = 1;
};

struct BuildOptions {
    uint8_t version = 2;
    uint16_t alignment = 0x20;
    uint16_t subkey = 0;
    uint8_t id_size = 2;
    uint8_t offset_size = 4;
};

[[nodiscard]] std::optional<uint64_t> choose_wave_id(
    QWidget* parent,
    const cricodecs::awb::AwbContainer& awb,
    uint32_t index
);

[[nodiscard]] std::optional<BatchWaveIdOptions> choose_batch_wave_ids(QWidget* parent);

[[nodiscard]] std::optional<BuildOptions> choose_build_options(
    QWidget* parent,
    const cricodecs::awb::AwbContainer& awb
);

void populate_editor_archive_table(
    QTableWidget* table,
    const cricodecs::awb::AwbContainer& awb,
    const DecryptionKeys& keys
);

} // namespace cristudio::modules::awb
