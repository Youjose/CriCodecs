#pragma once

#include "adx_key_recovery.hpp"

namespace cricodecs::adx {

// Ciphertext-only type-9 solver used by recover_key() after header detection.
[[nodiscard]] std::expected<AdxRecoveryResult, std::string> recover_key_type9(
    std::span<const AdxRecoverySource> sources);

// Exact known-scale recovery retained for focused algorithm validation.
// Product callers should use recover_key(), which needs ciphertext only.
[[nodiscard]] std::expected<AdxRecoveryResult, std::string> recover_key_from_scales(
    std::span<const AdxRecoverySource> encrypted_sources,
    std::span<const std::span<const uint16_t>> plaintext_scales);

} // namespace cricodecs::adx
