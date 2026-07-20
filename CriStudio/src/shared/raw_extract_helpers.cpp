#include "shared/raw_extract_helpers.hpp"

#include "modules/adx/adx_common.hpp"
#include "shared/document_sniffer.hpp"

#include "adx_codec.hpp"
#include "hca_codec.hpp"

namespace cristudio {

std::expected<std::vector<uint8_t>, std::string> raw_extract_transform(
    std::span<const uint8_t> bytes,
    const DecryptionKeys& keys
) {
    const auto maybe_adx = bytes.size() >= 4 && bytes[0] == 0x80 && bytes[1] == 0x00;
    const auto maybe_hca = has_hca_signature(bytes);

    if (maybe_adx && keys.adx_mode != DecryptionKeys::AdxMode::None) {
        if (auto adx = cricodecs::adx::Adx::load(bytes)) {
            modules::adx::apply_keys(*adx, keys);
            if (modules::adx::has_applicable_raw_key(*adx, keys)) {
                return adx->decrypt();
            }
        }
    }
    if (maybe_hca) {
        if (auto hca = cricodecs::hca::Hca::load(bytes)) {
            if (hca->header().cipher.encrypted()) {
                if (hca->header().cipher.type == 1 || keys.has_cri_key) {
                    return hca->decrypt(keys.has_cri_key ? keys.cri_key : 0, keys.hca_subkey);
                }
            }
        }
    }
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

} // namespace cristudio
