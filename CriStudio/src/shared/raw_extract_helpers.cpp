#include "shared/raw_extract_helpers.hpp"

#include "modules/adx/adx_common.hpp"
#include "shared/document_sniffer.hpp"

#include "adx_codec.hpp"
#include "hca_codec.hpp"

#include <utility>

namespace cristudio {
namespace {

std::expected<std::vector<uint8_t>, std::string> transform_owned_bytes(
    std::vector<uint8_t> bytes,
    const DecryptionKeys& keys
) {
    const auto view = std::span<const uint8_t>(bytes);
    const auto maybe_adx = view.size() >= 4 && view[0] == 0x80 && view[1] == 0x00;
    const auto maybe_hca = has_hca_signature(view);

    if (maybe_adx && keys.adx_mode != DecryptionKeys::AdxMode::None) {
        if (auto adx = cricodecs::adx::Adx::load(view)) {
            modules::adx::apply_keys(*adx, keys);
            if (modules::adx::has_applicable_raw_key(*adx, keys)) {
                return adx->decrypt();
            }
        }
    }
    if (maybe_hca) {
        if (auto hca = cricodecs::hca::Hca::load(view)) {
            if (hca->header().cipher.encrypted()) {
                if (hca->header().cipher.type == 1 || keys.has_cri_key) {
                    return hca->decrypt(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
                }
            }
        }
    }
    return bytes;
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> raw_extract_transform(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    return transform_owned_bytes(std::vector<uint8_t>(bytes.begin(), bytes.end()), keys);
}

std::expected<std::vector<uint8_t>, std::string> raw_extract_transform(
    std::vector<uint8_t>&& bytes,
    const DecryptionKeys& keys
) {
    return transform_owned_bytes(std::move(bytes), keys);
}

} // namespace cristudio
