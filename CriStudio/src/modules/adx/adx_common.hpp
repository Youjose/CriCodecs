#pragma once

#include "document/document_types.hpp"

#include "adx_codec.hpp"

#include <QString>

#include <cstdint>
#include <span>

namespace cristudio::modules::adx {

void apply_keys(cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] bool has_compatible_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] bool has_applicable_raw_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys);
[[nodiscard]] QString payload_preview(QString label, std::span<const uint8_t> bytes);

} // namespace cristudio::modules::adx
