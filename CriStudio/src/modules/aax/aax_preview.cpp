#include "modules/aax/aax_preview.hpp"

#include "modules/adx/adx_preview.hpp"

namespace cristudio::modules::aax {

std::expected<AudioPreview, std::string> audio_preview(
    const cricodecs::aax::AaxContainer& aax,
    const DecryptionKeys& keys
) {
    auto adx_bytes = aax.adx_data();
    if (!adx_bytes) {
        return std::unexpected("AAX preview failed: " + adx_bytes.error());
    }
    return modules::adx::audio_preview_from_bytes(*adx_bytes, keys);
}

std::expected<AudioPreview, std::string> audio_preview_from_bytes(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    auto aax = cricodecs::aax::AaxContainer::load(bytes);
    if (!aax) {
        return std::unexpected("AAX preview failed: " + aax.error());
    }
    return audio_preview(*aax, keys);
}

std::expected<AudioPreview, std::string> audio_preview_from_file(
    const std::filesystem::path& path,
    const DecryptionKeys& keys
) {
    auto aax = cricodecs::aax::AaxContainer::load(path);
    if (!aax) {
        return std::unexpected("AAX preview failed: " + aax.error());
    }
    return audio_preview(*aax, keys);
}

} // namespace cristudio::modules::aax
