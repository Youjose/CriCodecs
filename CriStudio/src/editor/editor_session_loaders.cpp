#include "editor/editor_session_loaders.hpp"

#include "editor_workspace.hpp"
#include "path_text.hpp"
#include "shared/document_sniffer.hpp"

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace cristudio {
namespace {

bool has_magic(std::span<const uint8_t> bytes, std::initializer_list<uint8_t> magic) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    size_t index = 0;
    for (const auto byte : magic) {
        if (bytes[index++] != byte) {
            return false;
        }
    }
    return true;
}

bool has_ascii_magic(std::span<const uint8_t> bytes, std::string_view magic) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    for (size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != static_cast<uint8_t>(magic[index])) {
            return false;
        }
    }
    return true;
}

void push_log(TransformEditorLoad& load, QString message) {
    load.log_messages.push_back(std::move(message));
}

void push_log(ArchiveEditorLoad& load, QString message) {
    load.log_messages.push_back(std::move(message));
}

} // namespace

TransformEditorLoad try_load_transform_editor_session(
    std::span<const uint8_t> bytes,
    const EditorOpenRequest& request,
    const std::filesystem::path* source_path
) {
    TransformEditorLoad load;
    if (bytes.empty()) {
        return load;
    }

    const auto format = editor_format_label(request).toLower();
    const bool has_adx_magic = has_magic(bytes, {0x80, 0x00});
    const bool has_aix_magic = has_ascii_magic(bytes, "AIXF");
    const bool has_usm_magic = has_ascii_magic(bytes, "CRID") || has_ascii_magic(bytes, "SFSH");
    const bool has_sfd_magic = has_magic(bytes, {0x00, 0x00, 0x01, 0xBA});
    const bool has_hca_magic = has_hca_signature(bytes);

    if (format.contains(QStringLiteral("aax"))) {
        auto loaded = cricodecs::aax::AaxContainer::load(bytes);
        if (loaded) {
            load.aax = std::move(*loaded);
            load.kind = TransformKind::Aax;
            push_log(load, QStringLiteral("AAX object loaded for segmented ADX inspection and export."));
        } else {
            push_log(load, QStringLiteral("AAX editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("usm")) || has_usm_magic) {
        cricodecs::usm::UsmReader loaded;
        loaded.set_key(request.keys.has_cri_key ? request.keys.cri_key : 0);
        const auto result =
            source_path != nullptr && !source_path->empty()
                ? loaded.load(*source_path)
                : loaded.load(bytes);
        if (result) {
            load.usm = std::move(loaded);
            load.kind = TransformKind::Usm;
            push_log(load, QStringLiteral("USM object loaded for chunk/stream inspection and extraction."));
        } else {
            push_log(load, QStringLiteral("USM editor load failed: %1").arg(utf8_to_qstring(result.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("sfd")) || has_sfd_magic) {
        auto loaded = cricodecs::sfd::SfdContainer::load(bytes);
        if (loaded) {
            load.sfd = std::move(*loaded);
            load.kind = TransformKind::Sfd;
            push_log(load, QStringLiteral("SFD object loaded for stream inspection, extraction, and rebuild."));
        } else {
            push_log(load, QStringLiteral("SFD editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("csb"))) {
        auto loaded = cricodecs::csb::CsbContainer::load(bytes);
        if (loaded) {
            load.csb = std::move(*loaded);
            load.kind = TransformKind::Csb;
            push_log(load, QStringLiteral("CSB object loaded for section/stream inspection, extraction, and rebuild."));
        } else {
            push_log(load, QStringLiteral("CSB editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("acb"))) {
        auto loaded = (source_path != nullptr && !source_path->empty())
            ? cricodecs::acb::AcbContainer::load(*source_path)
            : cricodecs::acb::AcbContainer::load(bytes);
        if (loaded) {
            load.acb = std::move(*loaded);
            load.kind = TransformKind::Acb;
            push_log(load, QStringLiteral("ACB object loaded for cue/waveform inspection and extraction."));
        } else {
            push_log(load, QStringLiteral("ACB editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("aix")) || has_aix_magic) {
        cricodecs::aix::Aix loaded;
        if (auto result = loaded.load(bytes); result) {
            load.aix = std::move(loaded);
            load.kind = TransformKind::Aix;
            push_log(load, QStringLiteral("AIX object loaded for layered ADX inspection and extraction."));
        } else {
            push_log(load, QStringLiteral("AIX editor load failed: %1").arg(utf8_to_qstring(result.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("hca")) || has_hca_magic) {
        auto loaded = cricodecs::hca::Hca::load(bytes);
        if (loaded) {
            load.hca = std::move(*loaded);
            load.kind = TransformKind::Hca;
            push_log(load, QStringLiteral("HCA object loaded for native transform actions."));
        } else {
            push_log(load, QStringLiteral("HCA editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("adx")) || format.contains(QStringLiteral("ahx")) || has_adx_magic) {
        auto loaded = cricodecs::adx::Adx::load(bytes);
        if (loaded) {
            load.adx = std::move(*loaded);
            load.kind = TransformKind::Adx;
            push_log(load, QStringLiteral("ADX/AHX object loaded for native transform actions."));
        } else {
            push_log(load, QStringLiteral("ADX/AHX editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
    }
    return load;
}

ArchiveEditorLoad try_load_archive_editor_session(std::span<const uint8_t> bytes, const EditorOpenRequest& request) {
    ArchiveEditorLoad load;
    if (bytes.empty()) {
        return load;
    }

    const auto format = editor_format_label(request).toLower();
    const bool has_afs_magic = has_ascii_magic(bytes, std::string_view("AFS\0", 4));
    const bool has_awb_magic = has_ascii_magic(bytes, "AFS2");
    const bool has_cpk_magic = has_ascii_magic(bytes, "CPK ");
    const bool has_cvm_magic = has_cvm_header(bytes);

    if (format.contains(QStringLiteral("cpk")) || has_cpk_magic) {
        auto loaded = cricodecs::cpk::Cpk::load(bytes);
        if (loaded) {
            load.cpk = std::move(*loaded);
            load.kind = ArchiveKind::Cpk;
            push_log(load, QStringLiteral("CPK object loaded for native-backed archive editing."));
        } else {
            push_log(load, QStringLiteral("CPK editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("cvm")) || has_cvm_magic) {
        auto loaded = cricodecs::cvm::CvmContainer::load(bytes);
        if (loaded) {
            load.cvm = std::move(*loaded);
            load.kind = ArchiveKind::Cvm;
            push_log(load, QStringLiteral("CVM object loaded for native-backed ROFS editing."));
        } else {
            push_log(load, QStringLiteral("CVM editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("awb")) || format.contains(QStringLiteral("afs2")) || has_awb_magic) {
        auto loaded = cricodecs::awb::AwbContainer::load(bytes);
        if (loaded) {
            load.awb = std::move(*loaded);
            load.kind = ArchiveKind::Awb;
            push_log(load, QStringLiteral("AWB object loaded for native-backed archive editing."));
        } else {
            push_log(load, QStringLiteral("AWB editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("afs")) || has_afs_magic) {
        auto loaded = cricodecs::afs::AfsContainer::load(bytes);
        if (loaded) {
            load.afs = std::move(*loaded);
            load.kind = ArchiveKind::Afs;
            push_log(load, QStringLiteral("AFS object loaded for native-backed archive editing."));
        } else {
            push_log(load, QStringLiteral("AFS editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
        return load;
    }

    if (format.contains(QStringLiteral("acx"))) {
        auto loaded = cricodecs::acx::AcxContainer::load(bytes);
        if (loaded) {
            load.acx = std::move(*loaded);
            load.kind = ArchiveKind::Acx;
            push_log(load, QStringLiteral("ACX object loaded for native-backed archive editing."));
        } else {
            push_log(load, QStringLiteral("ACX editor load failed: %1").arg(utf8_to_qstring(loaded.error())));
        }
    }
    return load;
}

} // namespace cristudio
