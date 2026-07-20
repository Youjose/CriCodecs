#include "main_window.hpp"

#include "editor_workspace.hpp"
#include "path_text.hpp"
#include "cvm_builder.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QStringList>
#include <QTabWidget>

#include <array>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

std::string qt_to_utf8_local(const QString& text) {
    const auto utf8 = text.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

std::string default_cvm_disc_name_local(const std::filesystem::path& input_dir) {
    QString base = QFileInfo(path_to_qstring(input_dir)).fileName();
    if (base.trimmed().isEmpty()) {
        base = QStringLiteral("volume");
    }
    if (base.size() > 28) {
        base.truncate(28);
    }
    return qt_to_utf8_local(base + QStringLiteral(".cvm"));
}

std::optional<cricodecs::cpk::CpkPreset> choose_new_cpk_preset(QWidget* parent) {
    const std::array presets = {
        cricodecs::cpk::CpkPreset::Id,
        cricodecs::cpk::CpkPreset::Filename,
        cricodecs::cpk::CpkPreset::FilenameId,
        cricodecs::cpk::CpkPreset::FilenameGroup,
        cricodecs::cpk::CpkPreset::IdGroup,
        cricodecs::cpk::CpkPreset::FilenameIdGroup,
    };
    const QStringList labels = {
        QStringLiteral("ID"),
        QStringLiteral("Filename"),
        QStringLiteral("Filename + ID"),
        QStringLiteral("Filename + Group"),
        QStringLiteral("ID + Group"),
        QStringLiteral("Filename + ID + Group"),
    };

    bool accepted = false;
    const auto selected = QInputDialog::getItem(
        parent,
        QStringLiteral("New CPK archive"),
        QStringLiteral("Preset"),
        labels,
        1,
        false,
        &accepted);
    if (!accepted) {
        return std::nullopt;
    }
    const auto index = labels.indexOf(selected);
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return std::nullopt;
    }
    return presets[static_cast<size_t>(index)];
}

} // namespace

void MainWindow::open_files() {
    const auto files = QFileDialog::getOpenFileNames(this, QStringLiteral("Open CRI files"));
    std::vector<std::filesystem::path> paths;
    paths.reserve(files.size());
    for (const auto& file : files) {
        paths.emplace_back(path_from_qstring(file));
    }
    start_loading_paths(std::move(paths));
}

void MainWindow::open_folder() {
    const auto dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open folder"));
    if (!dir.isEmpty()) {
        start_loading_paths({path_from_qstring(dir)});
    }
}

void MainWindow::new_utf_editor_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_scratch_utf();
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created scratch UTF editor document"));
}

void MainWindow::new_afs_editor_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_scratch_afs();
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created scratch AFS editor document"));
}

void MainWindow::open_scratch_afs_editor() {
    new_afs_editor_document();
}

void MainWindow::new_awb_editor_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_scratch_awb();
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created scratch AWB/AFS2 editor document"));
}

void MainWindow::open_scratch_awb_editor() {
    new_awb_editor_document();
}

void MainWindow::new_acx_editor_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_scratch_acx();
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created scratch ACX editor document"));
}

void MainWindow::open_scratch_acx_editor() {
    new_acx_editor_document();
}

void MainWindow::new_cpk_editor_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    const auto preset = choose_new_cpk_preset(this);
    if (!preset) {
        return;
    }
    m_editor_workspace->create_scratch_cpk(*preset);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created scratch CPK editor document"));
}

void MainWindow::open_scratch_cpk_editor() {
    new_cpk_editor_document();
}

void MainWindow::new_audio_encode_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_audio_encode_job(m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created audio encode editor job"));
}

void MainWindow::open_audio_encode_editor() {
    new_audio_encode_document();
}

void MainWindow::new_media_build_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_media_build_job(m_decryption_keys, false);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created USM/SFD media build editor job"));
}

void MainWindow::new_sfd_build_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_media_build_job(m_decryption_keys, true);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Opened SFD build wizard"));
}

void MainWindow::open_media_build_editor() {
    new_media_build_document();
}

void MainWindow::new_aax_build_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_aax_build_job(m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created AAX ADX build editor job"));
}

void MainWindow::open_aax_build_editor() {
    new_aax_build_document();
}

void MainWindow::new_aix_build_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_aix_build_job(m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created AIX ADX build editor job"));
}

void MainWindow::open_aix_build_editor() {
    new_aix_build_document();
}

void MainWindow::new_csb_build_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    m_editor_workspace->create_csb_build_job(m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created CSB folder build editor job"));
}

void MainWindow::open_csb_build_editor() {
    new_csb_build_document();
}

void MainWindow::new_cvm_from_script_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    const auto script = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("New CVM from build script"),
        QString{},
        QStringLiteral("CVM scripts (*.cvs);;All files (*)")
    );
    if (script.isEmpty()) {
        return;
    }
    m_editor_workspace->create_cvm_from_script(path_from_qstring(script), m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created CVM editor document from %1").arg(script));
}

void MainWindow::new_cvm_from_directory_document() {
    if (m_editor_workspace == nullptr) {
        return;
    }
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("New CVM from folder"));
    if (directory.isEmpty()) {
        return;
    }

    cricodecs::cvm::CvmBuildDirectoryOptions options;
    const auto input_dir = path_from_qstring(directory);
    options.disc_name = default_cvm_disc_name_local(input_dir);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("CVM folder build options"));
    auto* layout = new QFormLayout(&dialog);
    auto* disc_name = new QLineEdit(utf8_to_qstring(options.disc_name), &dialog);
    auto* recording_date = new QLineEdit(&dialog);
    recording_date->setPlaceholderText(QStringLiteral("YYYY-MM-DD HH:MM:SS or SDK dd/mm/yyyy HH:MM:SS:0:0"));
    auto* media = new QComboBox(&dialog);
    media->addItem(QStringLiteral("DVD"));
    media->addItem(QStringLiteral("CD"));
    auto* system_identifier = new QLineEdit(QStringLiteral("CRI ROFS"), &dialog);
    auto* volume_identifier = new QLineEdit(&dialog);
    auto* volume_set_identifier = new QLineEdit(&dialog);
    auto* publisher_identifier = new QLineEdit(&dialog);
    auto* data_preparer_identifier = new QLineEdit(&dialog);
    auto* application_identifier = new QLineEdit(&dialog);

    layout->addRow(QStringLiteral("Disc name"), disc_name);
    layout->addRow(QStringLiteral("Recording date"), recording_date);
    layout->addRow(QStringLiteral("Media"), media);
    layout->addRow(QStringLiteral("System identifier"), system_identifier);
    layout->addRow(QStringLiteral("Volume identifier"), volume_identifier);
    layout->addRow(QStringLiteral("Volume set identifier"), volume_set_identifier);
    layout->addRow(QStringLiteral("Publisher identifier"), publisher_identifier);
    layout->addRow(QStringLiteral("Data preparer identifier"), data_preparer_identifier);
    layout->addRow(QStringLiteral("Application identifier"), application_identifier);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    options.disc_name = qt_to_utf8_local(disc_name->text().trimmed());
    options.recording_date = qt_to_utf8_local(recording_date->text().trimmed());
    options.media = qt_to_utf8_local(media->currentText());
    options.system_identifier = qt_to_utf8_local(system_identifier->text().trimmed());
    options.volume_identifier = qt_to_utf8_local(volume_identifier->text().trimmed());
    options.volume_set_identifier = qt_to_utf8_local(volume_set_identifier->text().trimmed());
    options.publisher_identifier = qt_to_utf8_local(publisher_identifier->text().trimmed());
    options.data_preparer_identifier = qt_to_utf8_local(data_preparer_identifier->text().trimmed());
    options.application_identifier = qt_to_utf8_local(application_identifier->text().trimmed());

    m_editor_workspace->create_cvm_from_directory(input_dir, options, m_decryption_keys);
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QStringLiteral("Created CVM editor document from folder %1").arg(directory));
}

} // namespace cristudio
