#pragma once

#include "document_loader.hpp"
#include "cpk_container.hpp"
#include "cvm_builder.hpp"

#include <QWidget>

#include <filesystem>
#include <optional>

class QTabWidget;

namespace cristudio {

struct EditorOpenRequest {
    enum class SourceKind {
        Path,
        ArchiveEntry,
        Scratch
    };

    SourceKind source_kind = SourceKind::Path;
    std::string display_name;
    std::string detected_format;
    std::filesystem::path source_path;
    std::filesystem::path source_archive_path;
    std::string source_archive_format;
    uint32_t source_index = 0;
    DecryptionKeys keys;
    std::optional<LoadedDocument> document = std::nullopt;
    std::optional<EntrySummary> entry = std::nullopt;
    std::optional<std::vector<uint8_t>> source_bytes = std::nullopt;
    std::optional<cricodecs::cpk::CpkPreset> cpk_preset = std::nullopt;
    std::optional<cricodecs::cvm::CvmBuildDirectoryOptions> cvm_directory_options = std::nullopt;
    bool media_build_prefer_sfd = false;
};

class EditorWorkspace final : public QWidget {
public:
    explicit EditorWorkspace(QWidget* parent = nullptr);

    void open_request(EditorOpenRequest request);
    void create_scratch_utf();
    void create_scratch_afs();
    void create_scratch_awb();
    void create_scratch_acx();
    void create_scratch_cpk(cricodecs::cpk::CpkPreset preset);
    void create_audio_encode_job(const DecryptionKeys& keys);
    void create_media_build_job(const DecryptionKeys& keys, bool prefer_sfd = false);
    void create_aax_build_job(const DecryptionKeys& keys);
    void create_aix_build_job(const DecryptionKeys& keys);
    void create_csb_build_job(const DecryptionKeys& keys);
    void create_cvm_from_script(const std::filesystem::path& script_path, const DecryptionKeys& keys);
    void create_cvm_from_directory(
        const std::filesystem::path& input_dir,
        const cricodecs::cvm::CvmBuildDirectoryOptions& options,
        const DecryptionKeys& keys
    );

    [[nodiscard]] bool has_background_work() const;
    [[nodiscard]] bool has_dirty_documents() const;

private:
    void close_tab(int index);
    void update_close_buttons();
    QTabWidget* m_tabs = nullptr;
};

} // namespace cristudio
