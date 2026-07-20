#include "editor_workspace.hpp"

#include "editor/editor_document_widget.hpp"
#include "editor/editor_widgets.hpp"

#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace cristudio {

EditorWorkspace::EditorWorkspace(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("EditorTabs"));
    m_tabs->setDocumentMode(false);
    m_tabs->setTabsClosable(false);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    m_tabs->tabBar()->setUsesScrollButtons(true);
    layout->addWidget(m_tabs);

    add_editor_start_tab(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { update_close_buttons(); });
}

void EditorWorkspace::close_tab(int index) {
    auto* widget = m_tabs == nullptr ? nullptr : m_tabs->widget(index);
    if (widget == nullptr || (is_editor_document_widget(widget) && !editor_document_confirm_close(widget, this))) {
        return;
    }
    remove_editor_tab(m_tabs, widget);
    update_close_buttons();
}

void EditorWorkspace::update_close_buttons() {
    if (m_tabs == nullptr) {
        return;
    }
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* previous = m_tabs->tabBar()->tabButton(index, QTabBar::RightSide);
        m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
        if (previous != nullptr) {
            previous->deleteLater();
        }
    }
    const auto current = m_tabs->currentIndex();
    if (current < 0 || !is_editor_document_widget(m_tabs->widget(current))) {
        return;
    }
    auto* close = new QToolButton(m_tabs->tabBar());
    close->setObjectName(QStringLiteral("EditorTabCloseButton"));
    close->setText(QStringLiteral("\u00d7"));
    close->setToolTip(QStringLiteral("Close document"));
    close->setAccessibleName(QStringLiteral("Close current editor document"));
    connect(close, &QToolButton::clicked, this, [this, document = m_tabs->widget(current)] {
        close_tab(m_tabs->indexOf(document));
    });
    m_tabs->tabBar()->setTabButton(current, QTabBar::RightSide, close);
}

void EditorWorkspace::open_request(EditorOpenRequest request) {
    if (m_tabs->count() == 1 && !is_editor_document_widget(m_tabs->widget(0))) {
        auto* first = m_tabs->widget(0);
        m_tabs->removeTab(0);
        first->deleteLater();
    }

    auto* document = create_editor_document_widget(std::move(request), m_tabs);
    const auto index = m_tabs->addTab(document, editor_document_tab_title(document));
    m_tabs->setCurrentIndex(index);
    update_close_buttons();
}

void EditorWorkspace::create_scratch_utf() {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "New UTF Table";
    request.detected_format = "UTF table";
    open_request(std::move(request));
}

void EditorWorkspace::create_scratch_afs() {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "New AFS Archive";
    request.detected_format = "AFS archive";
    open_request(std::move(request));
}

void EditorWorkspace::create_scratch_awb() {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "New AWB/AFS2 Archive";
    request.detected_format = "AWB/AFS2 archive";
    open_request(std::move(request));
}

void EditorWorkspace::create_scratch_acx() {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "New ACX Archive";
    request.detected_format = "ACX archive";
    open_request(std::move(request));
}

void EditorWorkspace::create_scratch_cpk(cricodecs::cpk::CpkPreset preset) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "New CPK Archive";
    request.detected_format = "CPK archive";
    request.cpk_preset = preset;
    open_request(std::move(request));
}

void EditorWorkspace::create_audio_encode_job(const DecryptionKeys& keys) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "Encode Audio";
    request.detected_format = "Audio encode job";
    request.keys = keys;
    open_request(std::move(request));
}

void EditorWorkspace::create_media_build_job(const DecryptionKeys& keys, bool prefer_sfd) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "Build Movie";
    request.detected_format = "USM/SFD build job";
    request.keys = keys;
    request.media_build_prefer_sfd = prefer_sfd;
    open_request(std::move(request));
}

void EditorWorkspace::create_aax_build_job(const DecryptionKeys& keys) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "Build AAX";
    request.detected_format = "AAX ADX build job";
    request.keys = keys;
    open_request(std::move(request));
}

void EditorWorkspace::create_aix_build_job(const DecryptionKeys& keys) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "Build AIX";
    request.detected_format = "AIX ADX build job";
    request.keys = keys;
    open_request(std::move(request));
}

void EditorWorkspace::create_csb_build_job(const DecryptionKeys& keys) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = "Build CSB";
    request.detected_format = "CSB folder build job";
    request.keys = keys;
    open_request(std::move(request));
}

void EditorWorkspace::create_cvm_from_script(const std::filesystem::path& script_path, const DecryptionKeys& keys) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = script_path.filename().string();
    request.detected_format = "CVM build script";
    request.source_path = script_path;
    request.keys = keys;
    open_request(std::move(request));
}

void EditorWorkspace::create_cvm_from_directory(
    const std::filesystem::path& input_dir,
    const cricodecs::cvm::CvmBuildDirectoryOptions& options,
    const DecryptionKeys& keys
) {
    EditorOpenRequest request;
    request.source_kind = EditorOpenRequest::SourceKind::Scratch;
    request.display_name = input_dir.filename().string();
    request.detected_format = "CVM directory";
    request.source_path = input_dir;
    request.keys = keys;
    request.cvm_directory_options = options;
    open_request(std::move(request));
}

bool EditorWorkspace::has_background_work() const {
    if (m_tabs == nullptr) {
        return false;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editor_document_has_background_work(m_tabs->widget(i))) {
            return true;
        }
    }
    return false;
}

bool EditorWorkspace::has_dirty_documents() const {
    if (m_tabs == nullptr) {
        return false;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editor_document_is_dirty(m_tabs->widget(i))) {
            return true;
        }
    }
    return false;
}

} // namespace cristudio
