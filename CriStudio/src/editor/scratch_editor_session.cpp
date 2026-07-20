#include "editor/scratch_editor_session.hpp"

#include "editor/editor_helpers.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/afs/afs_edit.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/utf/utf_edit.hpp"
#include "path_text.hpp"

#include "cvm_builder.hpp"

#include <QFileInfo>

#include <span>

namespace cristudio {
namespace {

void push_log(ScratchEditorSession& session, QString message) {
    session.log_messages.push_back(std::move(message));
}

LoadedDocument failed_cvm_script_document(
    const QString& script_path_text,
    std::string format,
    std::vector<InfoRow> info,
    uintmax_t file_size = 0
) {
    return LoadedDocument{
        .display_name = qstring_to_utf8(QFileInfo(script_path_text).fileName()),
        .format = std::move(format),
        .file_size = file_size,
        .info = std::move(info)
    };
}

} // namespace

ScratchEditorSession create_scratch_editor_session(const EditorOpenRequest& request) {
    ScratchEditorSession session;

    if (request.detected_format == "Audio encode job") {
        session.transform_kind = TransformKind::AudioEncode;
        session.document = LoadedDocument{
            .display_name = "EncodeAudio",
            .format = "Audio encode job",
            .file_size = 0,
            .info = {
                {"Source", "Scratch audio encode job"},
                {"Targets", "ADX, AHX, HCA"},
                {"Input", "WAV"}
            }
        };
        push_log(session, QStringLiteral("Created scratch audio encode job."));
        return session;
    }

    if (request.detected_format == "USM/SFD build job") {
        session.transform_kind = TransformKind::MediaBuild;
        session.document = LoadedDocument{
            .display_name = "BuildMovie",
            .format = "USM/SFD build job",
            .file_size = 0,
            .info = {
                {"Source", "Scratch USM/SFD build job"},
                {"Targets", "USM, SFD"},
                {"Video prep", "prepared, FFmpeg VP9, H.264, or MPEG"},
                {"Audio prep", "no tracks, or ADX/HCA from prepared or FFmpeg-supported audio"}
            }
        };
        push_log(session, QStringLiteral("Created scratch USM/SFD build job."));
        return session;
    }

    if (request.detected_format == "AAX ADX build job") {
        session.transform_kind = TransformKind::Aax;
        session.document = LoadedDocument{
            .display_name = "BuildAax",
            .format = "AAX ADX build job",
            .file_size = 0,
            .info = {
                {"Source", "Scratch AAX build job"},
                {"Input", "one ADX/AHX file per segment"},
                {"Output", "AAX UTF wrapper"},
                {"Loop", "optional last-segment loop marker"}
            }
        };
        push_log(session, QStringLiteral("Created scratch AAX ADX build job."));
        return session;
    }

    if (request.detected_format == "AIX ADX build job") {
        session.transform_kind = TransformKind::Aix;
        session.document = LoadedDocument{
            .display_name = "BuildAix",
            .format = "AIX ADX build job",
            .file_size = 0,
            .info = {
                {"Source", "Scratch AIX build job"},
                {"Input", "one line per segment, semicolon-separated ADX/AHX layers"},
                {"Output", "AIX layered ADX container"}
            }
        };
        push_log(session, QStringLiteral("Created scratch AIX ADX build job."));
        return session;
    }

    if (request.detected_format == "CSB folder build job") {
        session.transform_kind = TransformKind::Csb;
        session.document = LoadedDocument{
            .display_name = "BuildCsb",
            .format = "CSB folder build job",
            .file_size = 0,
            .info = {
                {"Source", "Scratch CSB folder build job"},
                {"Input", "folder tree of CSB payload files"},
                {"Output", "CSB cue/archive"}
            }
        };
        push_log(session, QStringLiteral("Created scratch CSB folder build job."));
        return session;
    }

    if (request.detected_format == "AFS archive") {
        auto scratch = modules::afs::create_scratch_archive();
        session.afs = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Afs;
        push_log(session, QStringLiteral("Created scratch AFS archive."));
        return session;
    }

    if (request.detected_format == "AWB/AFS2 archive") {
        auto scratch = modules::awb::create_scratch_archive();
        session.awb = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Awb;
        push_log(session, QStringLiteral("Created scratch AWB/AFS2 archive."));
        return session;
    }

    if (request.detected_format == "ACX archive") {
        auto scratch = modules::acx::create_scratch_archive();
        session.acx = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Acx;
        push_log(session, QStringLiteral("Created scratch ACX archive."));
        return session;
    }

    if (request.detected_format == "CPK archive") {
        auto scratch = modules::cpk::create_scratch_archive(
            request.cpk_preset.value_or(cricodecs::cpk::CpkPreset::Filename));
        session.cpk = std::move(scratch.container);
        session.document = std::move(scratch.document);
        session.archive_kind = ArchiveKind::Cpk;
        push_log(session, QStringLiteral("Created scratch CPK archive."));
        return session;
    }

    if (request.detected_format == "CVM build script") {
        const auto script_path_text = path_to_qstring(request.source_path);
        auto script = cricodecs::cvm::CvmBuildScript::load(request.source_path);
        if (!script) {
            session.document = failed_cvm_script_document(
                script_path_text,
                "CVM build script (failed)",
                {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Validation", script.error()}
                }
            );
            push_log(session, QStringLiteral("CVM CVS load failed: %1").arg(utf8_to_qstring(script.error())));
            return session;
        }

        auto built = cricodecs::cvm::CvmBuilder{}.build(*script);
        if (!built) {
            session.document = failed_cvm_script_document(
                script_path_text,
                "CVM build script (failed)",
                {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Disc", script->disc_name()},
                    {"Files", std::to_string(script->files().size())},
                    {"Validation", built.error()}
                }
            );
            push_log(session, QStringLiteral("CVM CVS build failed: %1").arg(utf8_to_qstring(built.error())));
            return session;
        }

        session.bytes = std::move(*built);
        auto loaded = cricodecs::cvm::CvmContainer::load(
            std::span<const uint8_t>(session.bytes.data(), session.bytes.size()));
        if (!loaded) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(script_path_text).completeBaseName() + QStringLiteral(".cvm")),
                .format = "CVM build script (failed)",
                .file_size = session.bytes.size(),
                .info = {
                    {"Source", qstring_to_utf8(script_path_text)},
                    {"Disc", script->disc_name()},
                    {"Files", std::to_string(script->files().size())},
                    {"Bytes", std::to_string(session.bytes.size())},
                    {"Validation", loaded.error()}
                }
            };
            push_log(session, QStringLiteral("CVM CVS reload failed: %1").arg(utf8_to_qstring(loaded.error())));
            return session;
        }

        session.cvm = std::move(*loaded);
        session.archive_kind = ArchiveKind::Cvm;
        session.document = LoadedDocument{
            .display_name = qstring_to_utf8(QFileInfo(script_path_text).completeBaseName() + QStringLiteral(".cvm")),
            .format = "CVM/ROFS image (from CVS)",
            .file_size = session.bytes.size(),
            .info = {
                {"Source", qstring_to_utf8(script_path_text)},
                {"Disc", script->disc_name()},
                {"Media", script->media()},
                {"Files", std::to_string(script->files().size())},
                {"Bytes", std::to_string(session.bytes.size())}
            },
            .entry_columns = {"Index", "Archive Path", "Extent Sector", "Size"},
            .entry_column_types = {"integer", "path", "integer", "size"},
            .entries = {}
        };
        session.title = session.document->display_name;
        push_log(session, QStringLiteral("Built scratch CVM session from %1.").arg(script_path_text));
        return session;
    }

    if (request.detected_format == "CVM directory") {
        const auto input_dir_text = path_to_qstring(request.source_path);
        const auto options = request.cvm_directory_options.value_or(cricodecs::cvm::CvmBuildDirectoryOptions{});
        auto input = cricodecs::cvm::CvmBuildInput::from_directory(request.source_path, options);
        if (!input) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(input_dir_text).fileName() + QStringLiteral(".cvm")),
                .format = "CVM directory build (failed)",
                .file_size = 0,
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", options.disc_name},
                    {"Media", options.media},
                    {"Validation", input.error()}
                }
            };
            push_log(session, QStringLiteral("CVM directory scan failed: %1").arg(utf8_to_qstring(input.error())));
            return session;
        }

        auto built = cricodecs::cvm::CvmBuilder{}.build(*input);
        if (!built) {
            session.document = LoadedDocument{
                .display_name = qstring_to_utf8(QFileInfo(input_dir_text).fileName() + QStringLiteral(".cvm")),
                .format = "CVM directory build (failed)",
                .file_size = 0,
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", input->disc_name},
                    {"Media", input->media},
                    {"Files", std::to_string(input->files.size())},
                    {"Validation", built.error()}
                }
            };
            push_log(session, QStringLiteral("CVM directory build failed: %1").arg(utf8_to_qstring(built.error())));
            return session;
        }

        session.bytes = std::move(*built);
        auto loaded = cricodecs::cvm::CvmContainer::load(
            std::span<const uint8_t>(session.bytes.data(), session.bytes.size()));
        if (!loaded) {
            session.document = LoadedDocument{
                .display_name = input->disc_name,
                .format = "CVM directory build (failed)",
                .file_size = session.bytes.size(),
                .info = {
                    {"Source", qstring_to_utf8(input_dir_text)},
                    {"Disc", input->disc_name},
                    {"Media", input->media},
                    {"Files", std::to_string(input->files.size())},
                    {"Bytes", std::to_string(session.bytes.size())},
                    {"Validation", loaded.error()}
                }
            };
            push_log(session, QStringLiteral("CVM directory reload failed: %1").arg(utf8_to_qstring(loaded.error())));
            return session;
        }

        session.cvm = std::move(*loaded);
        session.archive_kind = ArchiveKind::Cvm;
        session.document = LoadedDocument{
            .display_name = input->disc_name,
            .format = "CVM/ROFS image (from directory)",
            .file_size = session.bytes.size(),
            .info = {
                {"Source", qstring_to_utf8(input_dir_text)},
                {"Disc", input->disc_name},
                {"Media", input->media},
                {"Files", std::to_string(input->files.size())},
                {"Bytes", std::to_string(session.bytes.size())}
            },
            .entry_columns = {"Index", "Archive Path", "Extent Sector", "Size"},
            .entry_column_types = {"integer", "path", "integer", "size"},
            .entries = {}
        };
        session.title = session.document->display_name;
        push_log(session, QStringLiteral("Built scratch CVM session from directory %1.").arg(input_dir_text));
        return session;
    }

    auto scratch = modules::utf::create_scratch_table_session();
    session.utf = std::move(scratch.table);
    session.has_utf = true;
    session.bytes = std::move(scratch.bytes);
    session.document = std::move(scratch.document);
    push_log(session, QStringLiteral("Created scratch UTF table."));
    return session;
}

} // namespace cristudio
