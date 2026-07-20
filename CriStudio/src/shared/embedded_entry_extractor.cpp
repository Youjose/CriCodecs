#include "shared/embedded_entry_extractor.hpp"

#include "modules/utf/utf_common.hpp"

#include "aax_container.hpp"
#include "acb_container.hpp"
#include "acx_container.hpp"
#include "afs_container.hpp"
#include "aix_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "csb_container.hpp"
#include "cvm_container.hpp"
#include "sfd_container.hpp"
#include "usm_container.hpp"
#include "utf_table.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cristudio {
namespace {

std::expected<std::vector<uint8_t>, std::string> extract_from_materialized_source(
    std::string_view source_format,
    std::span<const uint8_t> source_bytes,
    uint32_t source_index,
    const DecryptionKeys& keys,
    EmbeddedPayloadPurpose purpose
) {
    if (source_format == "AAX") {
        auto aax = cricodecs::aax::AaxContainer::load(source_bytes);
        if (!aax) {
            return std::unexpected(aax.error());
        }
        auto data = aax->segment_data(source_index);
        if (!data) {
            return std::unexpected(data.error());
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    if (source_format == "AIX") {
        cricodecs::aix::Aix aix;
        if (auto loaded = aix.load(source_bytes); !loaded) {
            return std::unexpected(loaded.error());
        }
        const auto layer_count = std::max<size_t>(aix.layers().size(), 1);
        const auto segment_index = source_index / layer_count;
        const auto layer_index = source_index % layer_count;
        return aix.segment_bytes(segment_index, layer_index);
    }
    if (source_format == "ACX") {
        auto acx = cricodecs::acx::AcxContainer::load(source_bytes);
        if (!acx) {
            return std::unexpected(acx.error());
        }
        auto data = acx->file_data(source_index);
        if (!data) {
            return std::unexpected(data.error());
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    if (source_format == "CPK") {
        auto cpk = cricodecs::cpk::Cpk::load(source_bytes);
        if (!cpk) {
            return std::unexpected(cpk.error());
        }
        return cpk->file_bytes(source_index);
    }
    if (source_format == "AFS") {
        auto afs = cricodecs::afs::AfsContainer::load(source_bytes);
        if (!afs) {
            return std::unexpected(afs.error());
        }
        auto data = afs->file_data(source_index);
        if (!data) {
            return std::unexpected(data.error());
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    if (source_format == "AWB") {
        auto awb = cricodecs::awb::AwbContainer::load(source_bytes);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->file_bytes(source_index);
    }
    if (source_format == "ACB") {
        auto acb = cricodecs::acb::AcbContainer::load(source_bytes);
        if (!acb) {
            return std::unexpected(acb.error());
        }
        if (purpose == EmbeddedPayloadPurpose::Playback) {
            return acb->extract_waveform_stream_data(source_index, keys.has_cri_key ? keys.cri_key : 0);
        }
        return acb->extract_waveform_data(source_index, keys.has_cri_key ? keys.cri_key : 0);
    }
    if (source_format == "CSB") {
        auto csb = cricodecs::csb::CsbContainer::load(source_bytes);
        if (!csb) {
            return std::unexpected(csb.error());
        }
        return csb->stream_data(source_index);
    }
    if (source_format == "USM") {
        cricodecs::usm::UsmReader usm;
        if (auto loaded = usm.load(source_bytes); !loaded) {
            return std::unexpected(loaded.error());
        }
        if (keys.has_cri_key) {
            usm.set_key(keys.cri_key);
        }
        return usm.extract_stream(source_index);
    }
    if (source_format == "SFD") {
        auto sfd = cricodecs::sfd::SfdContainer::load(source_bytes);
        if (!sfd) {
            return std::unexpected(sfd.error());
        }
        return sfd->extract_stream(source_index);
    }
    if (source_format == "CVM") {
        auto cvm = cricodecs::cvm::CvmContainer::load(source_bytes);
        if (!cvm) {
            return std::unexpected(cvm.error());
        }
        auto data = cvm->file_data(source_index);
        if (!data) {
            return std::unexpected(data.error());
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    if (source_format == "UTF") {
        auto utf = cricodecs::utf::UtfTable::load(source_bytes);
        if (!utf) {
            return std::unexpected(utf.error());
        }
        return modules::utf::extract_cell_data(*utf, source_index);
    }
    return std::unexpected("unsupported nested source: " + std::string(source_format));
}

} // namespace

class EmbeddedEntryExtractor::Impl {
public:
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract(
        const EntrySummary& entry,
        const DecryptionKeys& keys,
        EmbeddedPayloadPurpose purpose
    ) {
        if (!entry.has_source) {
            return std::unexpected("entry has no extractable source");
        }

        if (entry.has_nested_source) {
            auto outer_entry = entry;
            outer_entry.has_nested_source = false;
            outer_entry.nested_source_format.clear();
            outer_entry.nested_source_index = 0;
            auto outer_bytes = extract(outer_entry, keys, purpose);
            if (!outer_bytes) {
                return std::unexpected(outer_bytes.error());
            }

            return extract_from_materialized_source(
                entry.nested_source_format,
                *outer_bytes,
                entry.nested_source_index,
                keys,
                purpose
            );
        }

        if (entry.source_format == "AAX") {
            auto aax = cached_aax(entry.source_path);
            if (!aax) {
                return std::unexpected(aax.error());
            }
            auto data = (*aax)->segment_data(entry.source_index);
            if (!data) {
                return std::unexpected(data.error());
            }
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        if (entry.source_format == "AIX") {
            auto aix = cached_aix(entry.source_path);
            if (!aix) {
                return std::unexpected(aix.error());
            }
            const auto layer_count = std::max<size_t>((*aix)->layers().size(), 1);
            const auto segment_index = entry.source_index / layer_count;
            const auto layer_index = entry.source_index % layer_count;
            return (*aix)->segment_bytes(segment_index, layer_index);
        }
        if (entry.source_format == "ACX") {
            auto acx = cached_acx(entry.source_path);
            if (!acx) {
                return std::unexpected(acx.error());
            }
            auto data = (*acx)->file_data(entry.source_index);
            if (!data) {
                return std::unexpected(data.error());
            }
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        if (entry.source_format == "CPK") {
            auto cpk = cached_cpk(entry.source_path);
            if (!cpk) {
                return std::unexpected(cpk.error());
            }
            return (*cpk)->file_bytes(entry.source_index);
        }
        if (entry.source_format == "AFS") {
            auto afs = cached_afs(entry.source_path);
            if (!afs) {
                return std::unexpected(afs.error());
            }
            auto data = (*afs)->file_data(entry.source_index);
            if (!data) {
                return std::unexpected(data.error());
            }
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        if (entry.source_format == "AWB") {
            auto awb = cached_awb(entry.source_path);
            if (!awb) {
                return std::unexpected(awb.error());
            }
            return (*awb)->file_bytes(entry.source_index);
        }
        if (entry.source_format == "ACB") {
            auto acb = cached_acb(entry.source_path);
            if (!acb) {
                return std::unexpected(acb.error());
            }
            if (purpose == EmbeddedPayloadPurpose::Playback) {
                return (*acb)->extract_waveform_stream_data(entry.source_index, keys.has_cri_key ? keys.cri_key : 0);
            }
            return (*acb)->extract_waveform_data(entry.source_index, keys.has_cri_key ? keys.cri_key : 0);
        }
        if (entry.source_format == "CSB") {
            auto csb = cached_csb(entry.source_path);
            if (!csb) {
                return std::unexpected(csb.error());
            }
            return (*csb)->stream_data(entry.source_index);
        }
        if (entry.source_format == "USM") {
            auto usm = cached_usm(entry.source_path, keys);
            if (!usm) {
                return std::unexpected(usm.error());
            }
            return (*usm)->extract_stream(entry.source_index);
        }
        if (entry.source_format == "SFD") {
            auto sfd = cached_sfd(entry.source_path);
            if (!sfd) {
                return std::unexpected(sfd.error());
            }
            return (*sfd)->extract_stream(entry.source_index);
        }
        if (entry.source_format == "CVM") {
            auto cvm = cached_cvm(entry.source_path);
            if (!cvm) {
                return std::unexpected(cvm.error());
            }
            auto data = (*cvm)->file_data(entry.source_index);
            if (!data) {
                return std::unexpected(data.error());
            }
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        if (entry.source_format == "UTF") {
            auto utf = cached_utf(entry.source_path);
            if (!utf) {
                return std::unexpected(utf.error());
            }
            return modules::utf::extract_cell_data(**utf, entry.source_index);
        }
        return std::unexpected("unsupported source archive: " + entry.source_format);
    }

private:
    [[nodiscard]] std::expected<cricodecs::aax::AaxContainer*, std::string> cached_aax(const std::filesystem::path& path) {
        return cached_load(m_aax, path, [](const std::filesystem::path& source) {
            return cricodecs::aax::AaxContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::aix::Aix*, std::string> cached_aix(const std::filesystem::path& path) {
        return cached_load_void(m_aix, path, [](cricodecs::aix::Aix& archive, const std::filesystem::path& source) {
            return archive.load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::acx::AcxContainer*, std::string> cached_acx(const std::filesystem::path& path) {
        return cached_load(m_acx, path, [](const std::filesystem::path& source) {
            return cricodecs::acx::AcxContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::cpk::Cpk*, std::string> cached_cpk(const std::filesystem::path& path) {
        return cached_load(m_cpk, path, [](const std::filesystem::path& source) {
            return cricodecs::cpk::Cpk::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::afs::AfsContainer*, std::string> cached_afs(const std::filesystem::path& path) {
        return cached_load(m_afs, path, [](const std::filesystem::path& source) {
            return cricodecs::afs::AfsContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::awb::AwbContainer*, std::string> cached_awb(const std::filesystem::path& path) {
        return cached_load(m_awb, path, [](const std::filesystem::path& source) {
            return cricodecs::awb::AwbContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::acb::AcbContainer*, std::string> cached_acb(const std::filesystem::path& path) {
        return cached_load(m_acb, path, [](const std::filesystem::path& source) {
            return cricodecs::acb::AcbContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::csb::CsbContainer*, std::string> cached_csb(const std::filesystem::path& path) {
        return cached_load(m_csb, path, [](const std::filesystem::path& source) {
            return cricodecs::csb::CsbContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::usm::UsmReader*, std::string> cached_usm(
        const std::filesystem::path& path,
        const DecryptionKeys& keys
    ) {
        return cached_load_void(m_usm, path, [&keys](cricodecs::usm::UsmReader& archive, const std::filesystem::path& source) {
            if (auto loaded = archive.load(source); !loaded) {
                return loaded;
            }
            if (keys.has_cri_key) {
                archive.set_key(keys.cri_key);
            }
            return std::expected<void, std::string>{};
        });
    }

    [[nodiscard]] std::expected<cricodecs::sfd::SfdContainer*, std::string> cached_sfd(const std::filesystem::path& path) {
        return cached_load(m_sfd, path, [](const std::filesystem::path& source) {
            return cricodecs::sfd::SfdContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::cvm::CvmContainer*, std::string> cached_cvm(
        const std::filesystem::path& path
    ) {
        return cached_load(m_cvm, path, [](const std::filesystem::path& source) {
            return cricodecs::cvm::CvmContainer::load(source);
        });
    }

    [[nodiscard]] std::expected<cricodecs::utf::UtfTable*, std::string> cached_utf(const std::filesystem::path& path) {
        return cached_load(m_utf, path, [](const std::filesystem::path& source) {
            return cricodecs::utf::UtfTable::load(source);
        });
    }

    [[nodiscard]] static std::string cache_key(const std::filesystem::path& path) {
        return path.lexically_normal().generic_string();
    }

    template <class Archive, class Loader>
    [[nodiscard]] std::expected<Archive*, std::string> cached_load(
        std::unordered_map<std::string, Archive>& cache,
        const std::filesystem::path& path,
        Loader&& loader
    ) {
        const auto key = cache_key(path);
        if (auto found = cache.find(key); found != cache.end()) {
            return &found->second;
        }

        auto loaded = loader(path);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        auto [it, inserted] = cache.emplace(key, std::move(*loaded));
        (void)inserted;
        return &it->second;
    }

    template <class Archive, class Loader>
    [[nodiscard]] std::expected<Archive*, std::string> cached_load_void(
        std::unordered_map<std::string, Archive>& cache,
        const std::filesystem::path& path,
        Loader&& loader
    ) {
        const auto key = cache_key(path);
        if (auto found = cache.find(key); found != cache.end()) {
            return &found->second;
        }

        Archive archive;
        if (auto loaded = loader(archive, path); !loaded) {
            return std::unexpected(loaded.error());
        }
        auto [it, inserted] = cache.emplace(key, std::move(archive));
        (void)inserted;
        return &it->second;
    }

    std::unordered_map<std::string, cricodecs::aax::AaxContainer> m_aax;
    std::unordered_map<std::string, cricodecs::aix::Aix> m_aix;
    std::unordered_map<std::string, cricodecs::acx::AcxContainer> m_acx;
    std::unordered_map<std::string, cricodecs::cpk::Cpk> m_cpk;
    std::unordered_map<std::string, cricodecs::afs::AfsContainer> m_afs;
    std::unordered_map<std::string, cricodecs::awb::AwbContainer> m_awb;
    std::unordered_map<std::string, cricodecs::acb::AcbContainer> m_acb;
    std::unordered_map<std::string, cricodecs::csb::CsbContainer> m_csb;
    std::unordered_map<std::string, cricodecs::usm::UsmReader> m_usm;
    std::unordered_map<std::string, cricodecs::sfd::SfdContainer> m_sfd;
    std::unordered_map<std::string, cricodecs::cvm::CvmContainer> m_cvm;
    std::unordered_map<std::string, cricodecs::utf::UtfTable> m_utf;
};

EmbeddedEntryExtractor::EmbeddedEntryExtractor()
    : m_impl(std::make_unique<Impl>()) {}

EmbeddedEntryExtractor::~EmbeddedEntryExtractor() = default;
EmbeddedEntryExtractor::EmbeddedEntryExtractor(EmbeddedEntryExtractor&&) noexcept = default;
EmbeddedEntryExtractor& EmbeddedEntryExtractor::operator=(EmbeddedEntryExtractor&&) noexcept = default;

std::expected<std::vector<uint8_t>, std::string> EmbeddedEntryExtractor::extract(
    const EntrySummary& entry,
    const DecryptionKeys& keys,
    EmbeddedPayloadPurpose purpose
) {
    return m_impl->extract(entry, keys, purpose);
}

std::expected<std::vector<uint8_t>, std::string> extract_embedded_entry_payload(
    const EntrySummary& entry,
    const DecryptionKeys& keys,
    EmbeddedPayloadPurpose purpose
) {
    EmbeddedEntryExtractor extractor;
    return extractor.extract(entry, keys, purpose);
}

} // namespace cristudio
