#pragma once

#include "editor/editor_helpers.hpp"

#include "aax_container.hpp"
#include "acb_container.hpp"
#include "acx_container.hpp"
#include "adx_codec.hpp"
#include "afs_container.hpp"
#include "aix_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "csb_container.hpp"
#include "cvm_container.hpp"
#include "hca_codec.hpp"
#include "sfd_container.hpp"
#include "usm_container.hpp"

#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cristudio {

struct EditorOpenRequest;

struct TransformEditorLoad {
    std::optional<cricodecs::adx::Adx> adx;
    std::optional<cricodecs::hca::Hca> hca;
    std::optional<cricodecs::aax::AaxContainer> aax;
    std::optional<cricodecs::aix::Aix> aix;
    std::optional<cricodecs::usm::UsmReader> usm;
    std::optional<cricodecs::sfd::SfdContainer> sfd;
    std::optional<cricodecs::csb::CsbContainer> csb;
    std::optional<cricodecs::acb::AcbContainer> acb;
    TransformKind kind = TransformKind::None;
    std::vector<QString> log_messages;
};

struct ArchiveEditorLoad {
    std::optional<cricodecs::afs::AfsContainer> afs;
    std::optional<cricodecs::awb::AwbContainer> awb;
    std::optional<cricodecs::acx::AcxContainer> acx;
    std::optional<cricodecs::cpk::Cpk> cpk;
    std::optional<cricodecs::cvm::CvmContainer> cvm;
    ArchiveKind kind = ArchiveKind::None;
    std::vector<QString> log_messages;
};

[[nodiscard]] TransformEditorLoad try_load_transform_editor_session(
    std::span<const uint8_t> bytes,
    const EditorOpenRequest& request,
    const std::filesystem::path* source_path = nullptr
);

[[nodiscard]] ArchiveEditorLoad try_load_archive_editor_session(
    std::span<const uint8_t> bytes,
    const EditorOpenRequest& request
);

} // namespace cristudio
