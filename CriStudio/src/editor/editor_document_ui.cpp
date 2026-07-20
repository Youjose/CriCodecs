#include "editor/editor_document_ui.hpp"

#include "editor/archive_editor_helpers.hpp"
#include "editor/editor_helpers.hpp"
#include "editor/hex_preview_widget.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/transform_editor_helpers.hpp"
#include "editor/transform_detail_model.hpp"
#include "editor_workspace.hpp"
#include "entry_table_model.hpp"
#include "modules/acb/acb_edit.hpp"
#include "modules/acx/acx_edit_ui.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/afs/afs_edit_ui.hpp"
#include "modules/afs/afs_edit.hpp"
#include "modules/awb/awb_edit_ui.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/cpk/cpk_edit_ui.hpp"
#include "modules/cvm/cvm_edit.hpp"
#include "modules/cvm/cvm_edit_ui.hpp"
#include "main_window/ui_helpers.hpp"
#include "path_text.hpp"
#include "acb_container.hpp"
#include "utf_table.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QSplitter>
#include <QTableWidget>
#include <QTableView>
#include <QToolButton>
#include <QTreeView>
#include <QVideoWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <vector>

namespace cristudio {
namespace {

class EditorTableWidget final : public QTableWidget {
public:
    explicit EditorTableWidget(QWidget* parent = nullptr)
        : QTableWidget(parent) {}

    void set_preserve_horizontal_selection(bool enabled) {
        m_preserve_horizontal_selection = enabled;
    }

protected:
    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        if (!m_preserve_horizontal_selection || horizontalScrollBar() == nullptr) {
            QTableWidget::scrollTo(index, hint);
            return;
        }
        const int previous = horizontalScrollBar()->value();
        QTableWidget::scrollTo(index, hint);
        horizontalScrollBar()->setValue(previous);
    }

private:
    bool m_preserve_horizontal_selection = false;
};

class EditorTableView final : public QTableView {
public:
    explicit EditorTableView(QWidget* parent = nullptr)
        : QTableView(parent) {}

protected:
    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        if (horizontalScrollBar() == nullptr) {
            QTableView::scrollTo(index, hint);
            return;
        }
        const int previous = horizontalScrollBar()->value();
        QTableView::scrollTo(index, hint);
        horizontalScrollBar()->setValue(previous);
    }
};

QPushButton* toolbar_button(QString text, QWidget* parent) {
    return new QPushButton(std::move(text), parent);
}

QTableWidget* editor_table(QWidget* parent, QAbstractItemView::SelectionBehavior selection, QAbstractItemView::EditTriggers edits) {
    auto* table = new EditorTableWidget(parent);
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(selection);
    table->setEditTriggers(edits);
    table->setTextElideMode(Qt::ElideRight);
    table->setWordWrap(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(28);
    return table;
}

QTableView* transform_table(QWidget* parent) {
    auto* table = new EditorTableView(parent);
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setTextElideMode(Qt::ElideRight);
    table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setColumnWidth(0, 260);
    return table;
}

bool matches_transform_filter(const modules::TransformDetailRow& detail, QStringView filter_text) {
    if (filter_text.isEmpty()) {
        return true;
    }
    return detail.field.contains(filter_text, Qt::CaseInsensitive) ||
        detail.value.contains(filter_text, Qt::CaseInsensitive);
}

void append_info_rows(std::vector<InfoRow>& rows, const std::vector<modules::TransformDetailRow>& details, size_t max_rows = 16) {
    size_t added = 0;
    size_t omitted = 0;
    for (const auto& detail : details) {
        if (detail.payload_kind != 0) {
            continue;
        }
        if (added >= max_rows) {
            ++omitted;
            continue;
        }
        rows.push_back({qstring_to_utf8(detail.field), qstring_to_utf8(detail.value)});
        ++added;
    }
    (void)omitted;
}

std::vector<InfoRow> document_info_rows(const EditorDocumentInfoView& view) {
    std::vector<InfoRow> rows;
    if (view.request == nullptr) {
        return rows;
    }

    rows.push_back({"Session", view.request->source_kind == EditorOpenRequest::SourceKind::Scratch ? "Scratch" : "Independent copy"});
    rows.push_back({"Format", editor_format_label(*view.request).toStdString()});
    rows.push_back({"Bytes", std::to_string(view.byte_count)});
    if (!view.request->source_path.empty()) {
        rows.push_back({"Source path", path_to_utf8(view.request->source_path)});
    }
    if (!view.request->source_archive_path.empty()) {
        rows.push_back({"Source archive", path_to_utf8(view.request->source_archive_path)});
    }
    if (!view.request->source_archive_format.empty()) {
        rows.push_back({"Archive format", view.request->source_archive_format});
    }
    rows.push_back({"Validation", view.has_utf ? "UTF native build path available" : "Inspection/Save As copy path"});

    if (view.has_utf && view.utf != nullptr) {
        rows.push_back({"Table", std::string(view.utf->table_name())});
        rows.push_back({"Version", std::to_string(view.utf->version())});
        rows.push_back({"Rows", std::to_string(view.utf->row_count())});
        rows.push_back({"Columns", std::to_string(view.utf->column_count())});
        rows.push_back({"Row width", std::to_string(view.utf->row_width())});
        rows.push_back({"Data alignment", std::to_string(view.utf->data_alignment())});
        const auto table_size = view.utf->table_size() != 0 ? view.utf->table_size() : static_cast<uint32_t>(view.byte_count);
        rows.push_back({"Table size", std::to_string(table_size)});
        rows.push_back({"Text encoding", view.utf->text_encoding() ? *view.utf->text_encoding() : "default"});
        rows.push_back({"Serialized bytes", std::to_string(view.byte_count)});
    } else if (view.transform_kind != TransformKind::None && view.transform != nullptr) {
        append_transform_info_rows(rows, view.transform_kind, *view.transform);
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Afs && view.archive->afs != nullptr) {
        rows.back().value = "AFS native archive build path available";
        rows.push_back({"Archive kind", "AFS"});
        append_info_rows(rows, modules::afs::detail_rows(*view.archive->afs));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Awb && view.archive->awb != nullptr) {
        rows.back().value = "AWB native archive build path available";
        rows.push_back({"Archive kind", "AWB/AFS2"});
        append_info_rows(rows, modules::awb::detail_rows(*view.archive->awb, view.request->keys));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Acx && view.archive->acx != nullptr) {
        rows.back().value = "ACX native rebuild path available";
        rows.push_back({"Archive kind", "ACX"});
        append_info_rows(rows, modules::acx::detail_rows(*view.archive->acx));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Cpk && view.archive->cpk != nullptr) {
        rows.back().value = "CPK native archive save path available";
        rows.push_back({"Archive kind", "CPK"});
        append_info_rows(rows, modules::cpk::detail_rows(*view.archive->cpk), 32);
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Cvm && view.archive->cvm != nullptr) {
        rows.back().value = "CVM native ROFS save path available";
        rows.push_back({"Archive kind", "CVM/ROFS"});
        append_info_rows(rows, modules::cvm::detail_rows(*view.archive->cvm), 32);
    }

    return rows;
}

} // namespace

EditorDocumentUi build_editor_document_ui(QWidget* parent) {
    EditorDocumentUi ui;

    auto* outer = new QVBoxLayout(parent);
    outer->setContentsMargins(8, 2, 8, 8);
    outer->setSpacing(6);

    auto* header = new QWidget(parent);
    auto* header_layout = new QGridLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setHorizontalSpacing(16);
    header_layout->setVerticalSpacing(2);
    ui.title_label = new QLabel(header);
    ui.title_label->setObjectName(QStringLiteral("DocumentTitle"));
    ui.subtitle_label = new QLabel(header);
    ui.subtitle_label->setObjectName(QStringLiteral("DocumentSubtitle"));
    header_layout->addWidget(ui.title_label, 0, 0);
    header_layout->addWidget(ui.subtitle_label, 1, 0);

    auto* info_content = new QWidget(header);
    ui.info_grid = new QGridLayout(info_content);
    ui.info_grid->setContentsMargins(0, 0, 0, 0);
    ui.info_grid->setHorizontalSpacing(12);
    ui.info_grid->setVerticalSpacing(2);
    ui.info_grid->setColumnStretch(1, 1);
    ui.info_grid->setColumnStretch(3, 1);
    header_layout->addWidget(info_content, 0, 1, 2, 1, Qt::AlignVCenter);

    auto* action_panel = new QWidget(header);
    auto* action_layout = new QGridLayout(action_panel);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setHorizontalSpacing(8);
    action_layout->setVerticalSpacing(6);

    ui.save_button = new QToolButton(action_panel);
    ui.save_button->setObjectName(QStringLiteral("ActionButton"));
    ui.save_button->setText(QStringLiteral("Save"));
    ui.save_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.save_button->setToolTip(QStringLiteral("Save to the last path chosen with Save As; asks for a path the first time."));
    action_layout->addWidget(ui.save_button, 0, 0);

    ui.save_as_button = new QToolButton(action_panel);
    ui.save_as_button->setObjectName(QStringLiteral("ActionButton"));
    ui.save_as_button->setText(QStringLiteral("Save As"));
    ui.save_as_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.save_as_button->setToolTip(QStringLiteral("Build or copy this independent editor session to a chosen path."));
    action_layout->addWidget(ui.save_as_button, 0, 1);

    ui.build_button = new QToolButton(action_panel);
    ui.build_button->setObjectName(QStringLiteral("ActionButton"));
    ui.build_button->setText(QStringLiteral("Build"));
    ui.build_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.build_button->setToolTip(QStringLiteral("Validate and rebuild the in-memory editor object."));
    action_layout->addWidget(ui.build_button, 1, 0);

    ui.extract_button = new QToolButton(action_panel);
    ui.extract_button->setObjectName(QStringLiteral("ActionButton"));
    ui.extract_button->setText(QStringLiteral("Extract"));
    ui.extract_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.extract_button->setToolTip(QStringLiteral("Write this independent editor session's current bytes to a chosen folder."));
    action_layout->addWidget(ui.extract_button, 1, 1);
    header_layout->addWidget(action_panel, 0, 2, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
    header_layout->setColumnStretch(1, 1);
    outer->addWidget(header);

    ui.utf_toolbar = new QWidget(parent);
    auto* utf_toolbar_layout = new QHBoxLayout(ui.utf_toolbar);
    utf_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    utf_toolbar_layout->setSpacing(8);
    utf_toolbar_layout->addWidget(dim_label(QStringLiteral("Table"), ui.utf_toolbar), 0);
    ui.table_name_edit = new QLineEdit(ui.utf_toolbar);
    ui.table_name_edit->setClearButtonEnabled(true);
    ui.table_name_edit->setToolTip(QStringLiteral("Rename the UTF table."));
    utf_toolbar_layout->addWidget(ui.table_name_edit, 2);
    ui.apply_table_name_button = toolbar_button(QStringLiteral("Rename Table"), ui.utf_toolbar);
    ui.add_row_button = toolbar_button(QStringLiteral("Add Row"), ui.utf_toolbar);
    ui.remove_row_button = toolbar_button(QStringLiteral("Remove Row"), ui.utf_toolbar);
    ui.add_column_button = toolbar_button(QStringLiteral("Add Column"), ui.utf_toolbar);
    ui.remove_column_button = toolbar_button(QStringLiteral("Remove Column"), ui.utf_toolbar);
    utf_toolbar_layout->addWidget(ui.apply_table_name_button, 0);
    utf_toolbar_layout->addWidget(ui.add_row_button, 0);
    utf_toolbar_layout->addWidget(ui.remove_row_button, 0);
    utf_toolbar_layout->addWidget(ui.add_column_button, 0);
    utf_toolbar_layout->addWidget(ui.remove_column_button, 0);
    utf_toolbar_layout->addStretch(1);
    ui.utf_toolbar->hide();
    outer->addWidget(ui.utf_toolbar);

    ui.archive_toolbar = new QWidget(parent);
    auto* archive_toolbar_layout = new QHBoxLayout(ui.archive_toolbar);
    archive_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    archive_toolbar_layout->setSpacing(8);
    ui.archive_kind_label = dim_label(QStringLiteral("Archive"), ui.archive_toolbar);
    archive_toolbar_layout->addWidget(ui.archive_kind_label, 0);
    ui.add_archive_file_button = toolbar_button(QStringLiteral("Add File"), ui.archive_toolbar);
    ui.replace_archive_file_button = toolbar_button(QStringLiteral("Replace File"), ui.archive_toolbar);
    ui.remove_archive_file_button = toolbar_button(QStringLiteral("Remove Entry"), ui.archive_toolbar);
    ui.move_archive_entry_up_button = toolbar_button(QStringLiteral("Move Up"), ui.archive_toolbar);
    ui.move_archive_entry_down_button = toolbar_button(QStringLiteral("Move Down"), ui.archive_toolbar);
    ui.rename_archive_entry_button = toolbar_button(QStringLiteral("Rename Entry"), ui.archive_toolbar);
    ui.reserve_afs_id_button = toolbar_button(QStringLiteral("Reserve ID"), ui.archive_toolbar);
    ui.set_afs_timestamp_button = toolbar_button(QStringLiteral("Set Timestamp"), ui.archive_toolbar);
    ui.set_archive_wave_id_button = toolbar_button(QStringLiteral("Set Wave ID"), ui.archive_toolbar);
    ui.batch_awb_wave_ids_button = toolbar_button(QStringLiteral("Batch Wave IDs"), ui.archive_toolbar);
    ui.archive_entry_options_button = toolbar_button(QStringLiteral("Entry Props"), ui.archive_toolbar);
    ui.archive_options_button = toolbar_button(QStringLiteral("Options"), ui.archive_toolbar);
    ui.import_afs_als_button = toolbar_button(QStringLiteral("Import ALS"), ui.archive_toolbar);
    ui.export_afs_header_button = toolbar_button(QStringLiteral("Export Header"), ui.archive_toolbar);
    ui.import_cvm_script_button = toolbar_button(QStringLiteral("Import CVS"), ui.archive_toolbar);
    ui.export_cvm_script_button = toolbar_button(QStringLiteral("Export CVS"), ui.archive_toolbar);
    ui.extract_archive_entry_button = toolbar_button(QStringLiteral("Extract Entry"), ui.archive_toolbar);
    ui.extract_raw_archive_entry_button = toolbar_button(QStringLiteral("Extract Raw"), ui.archive_toolbar);
    for (auto* button : {
        ui.add_archive_file_button, ui.replace_archive_file_button, ui.remove_archive_file_button,
        ui.move_archive_entry_up_button, ui.move_archive_entry_down_button, ui.rename_archive_entry_button,
        ui.reserve_afs_id_button, ui.set_afs_timestamp_button, ui.set_archive_wave_id_button,
        ui.batch_awb_wave_ids_button, ui.archive_entry_options_button, ui.archive_options_button,
        ui.import_afs_als_button, ui.export_afs_header_button, ui.import_cvm_script_button,
        ui.export_cvm_script_button, ui.extract_archive_entry_button, ui.extract_raw_archive_entry_button
    }) {
        archive_toolbar_layout->addWidget(button, 0);
    }
    archive_toolbar_layout->addStretch(1);
    ui.archive_toolbar->hide();
    outer->addWidget(ui.archive_toolbar);

    ui.transform_toolbar = new QWidget(parent);
    auto* transform_toolbar_layout = new QHBoxLayout(ui.transform_toolbar);
    transform_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    transform_toolbar_layout->setSpacing(8);
    ui.transform_kind_label = dim_label(QStringLiteral("Transform"), ui.transform_toolbar);
    transform_toolbar_layout->addWidget(ui.transform_kind_label, 0);
    ui.encode_transform_button = toolbar_button(QStringLiteral("Encode from WAV"), ui.transform_toolbar);
    ui.decode_transform_button = toolbar_button(QStringLiteral("Decode WAV"), ui.transform_toolbar);
    ui.decrypt_transform_button = toolbar_button(QStringLiteral("Decrypt"), ui.transform_toolbar);
    ui.encrypt_transform_button = toolbar_button(QStringLiteral("Encrypt"), ui.transform_toolbar);
    ui.rebuild_transform_button = toolbar_button(QStringLiteral("Rebuild"), ui.transform_toolbar);
    ui.transform_options_button = toolbar_button(QStringLiteral("Options"), ui.transform_toolbar);
    ui.extract_transform_button = toolbar_button(QStringLiteral("Extract"), ui.transform_toolbar);
    ui.adx_container_build_button = toolbar_button(QStringLiteral("Build from ADX"), ui.transform_toolbar);
    ui.csb_directory_build_button = toolbar_button(QStringLiteral("Build from Folder"), ui.transform_toolbar);
    ui.media_build_wizard_button = toolbar_button(QStringLiteral("Build Wizard"), ui.transform_toolbar);
    ui.editor_mux_preview_button = toolbar_button(QStringLiteral("Preview"), ui.transform_toolbar);
    ui.open_acb_awb_button = toolbar_button(QStringLiteral("Open AWB"), ui.transform_toolbar);
    ui.export_acb_awb_button = toolbar_button(QStringLiteral("Export AWB"), ui.transform_toolbar);
    ui.add_transform_entry_button = toolbar_button(QStringLiteral("Add"), ui.transform_toolbar);
    ui.replace_transform_entry_button = toolbar_button(QStringLiteral("Replace"), ui.transform_toolbar);
    ui.remove_transform_entry_button = toolbar_button(QStringLiteral("Remove"), ui.transform_toolbar);
    ui.move_transform_entry_up_button = toolbar_button(QStringLiteral("Up"), ui.transform_toolbar);
    ui.move_transform_entry_down_button = toolbar_button(QStringLiteral("Down"), ui.transform_toolbar);
    ui.rename_transform_entry_button = toolbar_button(QStringLiteral("Rename"), ui.transform_toolbar);
    ui.toggle_transform_entry_flag_button = toolbar_button(QStringLiteral("Toggle"), ui.transform_toolbar);
    ui.transform_filter_edit = new QLineEdit(ui.transform_toolbar);
    ui.transform_filter_edit->setClearButtonEnabled(true);
    ui.transform_filter_edit->setPlaceholderText(QStringLiteral("Filter rows"));
    ui.transform_filter_edit->setToolTip(QStringLiteral("Filter transform rows by field or value."));
    for (auto* button : {
        ui.encode_transform_button, ui.decode_transform_button, ui.decrypt_transform_button,
        ui.encrypt_transform_button, ui.rebuild_transform_button, ui.transform_options_button,
        ui.extract_transform_button, ui.adx_container_build_button, ui.csb_directory_build_button,
        ui.media_build_wizard_button, ui.editor_mux_preview_button, ui.open_acb_awb_button, ui.export_acb_awb_button,
        ui.add_transform_entry_button, ui.replace_transform_entry_button, ui.remove_transform_entry_button,
        ui.move_transform_entry_up_button, ui.move_transform_entry_down_button,
        ui.rename_transform_entry_button, ui.toggle_transform_entry_flag_button
    }) {
        transform_toolbar_layout->addWidget(button, 0);
    }
    transform_toolbar_layout->addWidget(ui.transform_filter_edit, 1);
    transform_toolbar_layout->addStretch(1);
    ui.transform_toolbar->hide();
    outer->addWidget(ui.transform_toolbar);

    ui.cri_key_panel = new QWidget(parent);
    auto* key_layout = new QHBoxLayout(ui.cri_key_panel);
    key_layout->setContentsMargins(0, 0, 0, 0);
    key_layout->setSpacing(6);
    ui.local_key_label = dim_label(QStringLiteral("Local CRI key"), ui.cri_key_panel);
    key_layout->addWidget(ui.local_key_label, 0);
    ui.local_key_type = new QComboBox(ui.cri_key_panel);
    ui.local_key_type->addItem(QStringLiteral("No key"), static_cast<int>(DecryptionKeys::AdxMode::None));
    ui.local_key_type->addItem(QStringLiteral("Type 8 string"), static_cast<int>(DecryptionKeys::AdxMode::Type8String));
    ui.local_key_type->addItem(QStringLiteral("Type 9 number"), static_cast<int>(DecryptionKeys::AdxMode::Type9Number));
    ui.local_key_type->addItem(QStringLiteral("Key triplet"), static_cast<int>(DecryptionKeys::AdxMode::AhxTriplet));
    ui.local_key_type->setToolTip(QStringLiteral("Choose the ADX/AHX key representation explicitly."));
    ui.local_key_type->hide();
    key_layout->addWidget(ui.local_key_type, 0);
    ui.cri_key_edit = new QLineEdit(ui.cri_key_panel);
    ui.cri_key_edit->setClearButtonEnabled(true);
    ui.cri_key_edit->setPlaceholderText(QStringLiteral("No key"));
    ui.cri_key_edit->setToolTip(QStringLiteral("This key belongs only to this editor tab and does not change the global CRI key."));
    key_layout->addWidget(ui.cri_key_edit, 1);
    ui.cri_key_base = new QComboBox(ui.cri_key_panel);
    ui.cri_key_base->addItem(QStringLiteral("hex"), 16);
    ui.cri_key_base->addItem(QStringLiteral("dec"), 10);
    key_layout->addWidget(ui.cri_key_base, 0);
    ui.adx_subkey_panel = new QWidget(ui.cri_key_panel);
    auto* adx_subkey_layout = new QHBoxLayout(ui.adx_subkey_panel);
    adx_subkey_layout->setContentsMargins(0, 0, 0, 0);
    adx_subkey_layout->setSpacing(4);
    adx_subkey_layout->addWidget(dim_label(QStringLiteral("Subkey"), ui.adx_subkey_panel));
    ui.adx_subkey_spin = new QSpinBox(ui.adx_subkey_panel);
    ui.adx_subkey_spin->setRange(0, std::numeric_limits<uint16_t>::max());
    adx_subkey_layout->addWidget(ui.adx_subkey_spin);
    ui.adx_subkey_panel->hide();
    key_layout->addWidget(ui.adx_subkey_panel, 0);
    ui.adx_triplet_panel = new QWidget(ui.cri_key_panel);
    auto* triplet_layout = new QHBoxLayout(ui.adx_triplet_panel);
    triplet_layout->setContentsMargins(0, 0, 0, 0);
    triplet_layout->setSpacing(4);
    const auto triplet_validator = [](QLineEdit* edit) {
        edit->setMaximumWidth(72);
        edit->setMaxLength(4);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{1,4}")), edit));
    };
    ui.adx_triplet_start = new QLineEdit(ui.adx_triplet_panel);
    ui.adx_triplet_mult = new QLineEdit(ui.adx_triplet_panel);
    ui.adx_triplet_add = new QLineEdit(ui.adx_triplet_panel);
    triplet_validator(ui.adx_triplet_start);
    triplet_validator(ui.adx_triplet_mult);
    triplet_validator(ui.adx_triplet_add);
    triplet_layout->addWidget(dim_label(QStringLiteral("Start"), ui.adx_triplet_panel));
    triplet_layout->addWidget(ui.adx_triplet_start);
    triplet_layout->addWidget(dim_label(QStringLiteral("Mult"), ui.adx_triplet_panel));
    triplet_layout->addWidget(ui.adx_triplet_mult);
    triplet_layout->addWidget(dim_label(QStringLiteral("Add"), ui.adx_triplet_panel));
    triplet_layout->addWidget(ui.adx_triplet_add);
    ui.adx_triplet_panel->hide();
    key_layout->addWidget(ui.adx_triplet_panel, 1);
    ui.cvm_scramble_panel = new QWidget(ui.cri_key_panel);
    auto* cvm_scramble_layout = new QHBoxLayout(ui.cvm_scramble_panel);
    cvm_scramble_layout->setContentsMargins(0, 0, 0, 0);
    cvm_scramble_layout->setSpacing(8);
    ui.cvm_scramble_check = new ToggleSwitch(ui.cvm_scramble_panel);
    ui.cvm_scramble_check->setAccessibleName(QStringLiteral("Scramble CVM metadata on save"));
    ui.cvm_scramble_check->setToolTip(QStringLiteral("Write a scrambled CVM TOC using this tab's key string. Reading remains automatic."));
    cvm_scramble_layout->addWidget(ui.cvm_scramble_check, 0);
    cvm_scramble_layout->addWidget(dim_label(QStringLiteral("Scramble on save"), ui.cvm_scramble_panel), 0);
    ui.cvm_scramble_panel->hide();
    key_layout->addWidget(ui.cvm_scramble_panel, 0);
    ui.apply_cri_key_button = toolbar_button(QStringLiteral("Apply locally"), ui.cri_key_panel);
    key_layout->addWidget(ui.apply_cri_key_button, 0);
    key_layout->addStretch(1);
    ui.cri_key_panel->hide();
    outer->addWidget(ui.cri_key_panel);

    auto* body = new QSplitter(Qt::Horizontal, parent);
    body->setHandleWidth(13);
    auto* data_stack = new QWidget(body);
    auto* data_stack_layout = new QVBoxLayout(data_stack);
    data_stack_layout->setContentsMargins(0, 0, 0, 0);
    data_stack_layout->setSpacing(0);

    ui.table_model = new EntryTableModel(data_stack);
    ui.table = new QTreeView(data_stack);
    ui.table->setModel(ui.table_model);
    ui.table->setAlternatingRowColors(true);
    ui.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.table->setUniformRowHeights(true);
    ui.table->setRootIsDecorated(true);
    ui.table->setSortingEnabled(true);
    ui.table->header()->setStretchLastSection(false);
    ui.table->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui.table->header()->setMinimumSectionSize(64);
    data_stack_layout->addWidget(ui.table);

    ui.utf_grid = editor_table(
        data_stack,
        QAbstractItemView::SelectItems,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.utf_grid->hide();
    data_stack_layout->addWidget(ui.utf_grid);

    ui.archive_table = editor_table(
        data_stack,
        QAbstractItemView::SelectRows,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.archive_table->hide();
    data_stack_layout->addWidget(ui.archive_table);

    ui.transform_model = new TransformDetailModel(data_stack);
    ui.transform_table = transform_table(data_stack);
    ui.transform_table->setModel(ui.transform_model);
    ui.transform_table->hide();
    data_stack_layout->addWidget(ui.transform_table);
    body->addWidget(data_stack);

    auto* inspector = new QWidget(body);
    auto* inspector_layout = new QVBoxLayout(inspector);
    inspector_layout->setContentsMargins(8, 0, 0, 0);
    inspector_layout->setSpacing(8);

    ui.log_toggle_button = new QToolButton(inspector);
    ui.log_toggle_button->setText(QStringLiteral("Log"));
    ui.log_toggle_button->setCheckable(true);
    ui.log_toggle_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.log_toggle_button->setToolTip(QStringLiteral("Show or hide editor session log."));
    inspector_layout->addWidget(ui.log_toggle_button, 0, Qt::AlignRight);

    auto* inspector_splitter = new QSplitter(Qt::Vertical, inspector);
    inspector_splitter->setHandleWidth(9);
    ui.preview_tabs = new QTabWidget(inspector_splitter);
    ui.preview_tabs->setObjectName(QStringLiteral("EditorPreviewTabs"));
    ui.preview_tabs->setDocumentMode(true);
    auto* preview_page = new QWidget(ui.preview_tabs);
    auto* preview_layout = new QVBoxLayout(preview_page);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(6);

    ui.mux_preview_panel = new QWidget(preview_page);
    auto* mux_preview_layout = new QVBoxLayout(ui.mux_preview_panel);
    mux_preview_layout->setContentsMargins(6, 6, 6, 6);
    mux_preview_layout->setSpacing(8);

    ui.mux_video_frame = new QWidget(ui.mux_preview_panel);
    ui.mux_video_frame->setObjectName(QStringLiteral("VideoFrame"));
    ui.mux_video_frame->setMinimumHeight(260);
    ui.mux_video_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* video_layout = new QVBoxLayout(ui.mux_video_frame);
    video_layout->setContentsMargins(0, 0, 0, 0);
    video_layout->setSpacing(0);
    ui.mux_video_widget = new QVideoWidget(ui.mux_video_frame);
    ui.mux_video_widget->setMinimumHeight(260);
    ui.mux_video_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui.mux_video_widget->setAspectRatioMode(Qt::KeepAspectRatio);
    video_layout->addWidget(ui.mux_video_widget);
    ui.mux_video_frame->hide();
    mux_preview_layout->addWidget(ui.mux_video_frame, 8);

    ui.media_controls_panel = new QWidget(ui.mux_preview_panel);
    ui.media_controls_panel->setObjectName(QStringLiteral("AudioPanel"));
    ui.media_controls_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* media_controls_layout = new QVBoxLayout(ui.media_controls_panel);
    media_controls_layout->setContentsMargins(10, 8, 10, 8);
    media_controls_layout->setSpacing(6);

    ui.mux_audio_row = new QWidget(ui.media_controls_panel);
    auto* mux_audio_layout = new QHBoxLayout(ui.mux_audio_row);
    mux_audio_layout->setContentsMargins(0, 0, 0, 0);
    mux_audio_layout->setSpacing(8);
    mux_audio_layout->addWidget(dim_label(QStringLiteral("Audio channel"), ui.mux_audio_row), 0);
    ui.mux_audio_combo = new QComboBox(ui.mux_audio_row);
    ui.mux_audio_combo->setObjectName(QStringLiteral("MuxAudioCombo"));
    ui.mux_audio_combo->setToolTip(QStringLiteral("Choose which audio stream to preview with the video."));
    mux_audio_layout->addWidget(ui.mux_audio_combo, 1);
    ui.mux_audio_row->hide();
    media_controls_layout->addWidget(ui.mux_audio_row);
    ui.mux_subtitle_row = new QWidget(ui.media_controls_panel);
    auto* mux_subtitle_layout = new QHBoxLayout(ui.mux_subtitle_row);
    mux_subtitle_layout->setContentsMargins(0, 0, 0, 0);
    mux_subtitle_layout->setSpacing(8);
    mux_subtitle_layout->addWidget(dim_label(QStringLiteral("Subtitles"), ui.mux_subtitle_row), 0);
    ui.mux_subtitle_combo = new QComboBox(ui.mux_subtitle_row);
    ui.mux_subtitle_combo->setObjectName(QStringLiteral("MuxSubtitleCombo"));
    ui.mux_subtitle_combo->setToolTip(QStringLiteral("Choose which subtitle track to display."));
    mux_subtitle_layout->addWidget(ui.mux_subtitle_combo, 1);
    ui.mux_subtitle_row->hide();
    media_controls_layout->addWidget(ui.mux_subtitle_row);
    auto* mux_controls = new QHBoxLayout();
    mux_controls->setContentsMargins(0, 0, 0, 0);
    mux_controls->setSpacing(8);
    ui.mux_play_button = new QToolButton(ui.media_controls_panel);
    ui.mux_play_button->setIcon(ui.media_controls_panel->style()->standardIcon(QStyle::SP_MediaPlay));
    ui.mux_play_button->setText(QStringLiteral("Play"));
    ui.mux_play_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ui.mux_play_button->setEnabled(false);
    ui.mux_status_label = new QLabel(QStringLiteral("No playable media selected"), ui.media_controls_panel);
    ui.mux_status_label->setObjectName(QStringLiteral("AudioStatus"));
    ui.mux_status_label->setWordWrap(true);
    auto* volume_label = new QLabel(ui.media_controls_panel);
    volume_label->setPixmap(ui.media_controls_panel->style()->standardIcon(QStyle::SP_MediaVolume).pixmap(16, 16));
    volume_label->setToolTip(QStringLiteral("Volume"));
    volume_label->setAccessibleName(QStringLiteral("Volume"));
    ui.media_volume_slider = new QSlider(Qt::Horizontal, ui.media_controls_panel);
    ui.media_volume_slider->setObjectName(QStringLiteral("VolumeSlider"));
    ui.media_volume_slider->setRange(0, 100);
    ui.media_volume_slider->setValue(80);
    ui.media_volume_slider->setFixedWidth(96);
    ui.media_volume_slider->setToolTip(QStringLiteral("Playback volume"));
    mux_controls->addWidget(ui.mux_play_button, 0);
    mux_controls->addWidget(ui.mux_status_label, 1);
    mux_controls->addWidget(volume_label, 0, Qt::AlignVCenter);
    mux_controls->addWidget(ui.media_volume_slider, 0, Qt::AlignVCenter);
    media_controls_layout->addLayout(mux_controls);
    ui.media_loop_row = new QWidget(ui.media_controls_panel);
    auto* loop_layout = new QVBoxLayout(ui.media_loop_row);
    loop_layout->setContentsMargins(0, 0, 0, 0);
    loop_layout->setSpacing(4);
    ui.media_loop_toggle = new QCheckBox(QStringLiteral("Loop selected range"), ui.media_loop_row);
    ui.media_loop_list = new QListWidget(ui.media_loop_row);
    ui.media_loop_list->setObjectName(QStringLiteral("LoopList"));
    ui.media_loop_list->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.media_loop_list->setAlternatingRowColors(false);
    ui.media_loop_list->setUniformItemSizes(false);
    ui.media_loop_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui.media_loop_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.media_loop_list->setMinimumHeight(42);
    ui.media_loop_list->setMaximumHeight(96);
    loop_layout->addWidget(ui.media_loop_toggle);
    loop_layout->addWidget(ui.media_loop_list);
    ui.media_loop_row->hide();
    media_controls_layout->addWidget(ui.media_loop_row);
    auto* seek_controls = new QHBoxLayout();
    seek_controls->setContentsMargins(0, 0, 0, 0);
    seek_controls->setSpacing(8);
    ui.media_seek_slider = new SeekSlider(Qt::Horizontal, ui.media_controls_panel);
    ui.media_seek_slider->setRange(0, 0);
    ui.media_seek_slider->setEnabled(false);
    ui.media_time_label = new QLabel(QStringLiteral("0:00 / 0:00"), ui.media_controls_panel);
    ui.media_time_label->setMinimumWidth(92);
    ui.media_time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    seek_controls->addWidget(ui.media_seek_slider, 1);
    seek_controls->addWidget(ui.media_time_label, 0);
    media_controls_layout->addLayout(seek_controls);
    mux_preview_layout->addWidget(ui.media_controls_panel, 0, Qt::AlignTop);
    ui.mux_preview_panel->hide();
    preview_layout->addWidget(ui.mux_preview_panel, 8);

    ui.field_model = new EntryTableModel(preview_page);
    ui.field_table = new QTreeView(preview_page);
    ui.field_table->setModel(ui.field_model);
    ui.field_table->setAlternatingRowColors(true);
    ui.field_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.field_table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.field_table->setRootIsDecorated(false);
    ui.field_table->setUniformRowHeights(true);
    ui.field_table->header()->setStretchLastSection(false);
    ui.field_table->hide();
    preview_layout->addWidget(ui.field_table, 4);

    ui.schema_table = editor_table(
        preview_page,
        QAbstractItemView::SelectRows,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.schema_table->setColumnCount(7);
    ui.schema_table->setHorizontalHeaderLabels({
        QStringLiteral("Column"),
        QStringLiteral("Type"),
        QStringLiteral("Flags"),
        QStringLiteral("Default"),
        QStringLiteral("Default Offset"),
        QStringLiteral("Row Offset"),
        QStringLiteral("Index")
    });
    ui.schema_table->setMinimumHeight(132);
    ui.schema_table->hide();
    preview_layout->addWidget(ui.schema_table, 3);

    ui.payload_table = editor_table(preview_page, QAbstractItemView::SelectItems, QAbstractItemView::NoEditTriggers);
    ui.payload_table->hide();
    preview_layout->addWidget(ui.payload_table, 5);

    auto* raw_page = new QWidget(ui.preview_tabs);
    auto* raw_layout = new QVBoxLayout(raw_page);
    raw_layout->setContentsMargins(0, 0, 0, 0);
    raw_layout->setSpacing(0);
    ui.hex_preview = new HexPreviewWidget(raw_page);
    ui.hex_preview->hide();
    raw_layout->addWidget(ui.hex_preview, 1);

    ui.utf_edit_panel = new QWidget(preview_page);
    auto* edit_layout = new QHBoxLayout(ui.utf_edit_panel);
    edit_layout->setContentsMargins(0, 0, 0, 0);
    edit_layout->setSpacing(6);
    edit_layout->addWidget(dim_label(QStringLiteral("Value"), ui.utf_edit_panel), 0);
    ui.value_type_label = dim_label(QString{}, ui.utf_edit_panel);
    ui.value_type_label->setMinimumWidth(42);
    edit_layout->addWidget(ui.value_type_label, 0);
    ui.value_edit = new QLineEdit(ui.utf_edit_panel);
    ui.value_edit->setClearButtonEnabled(true);
    ui.value_edit->setToolTip(QStringLiteral("Edit the selected UTF cell. Binary and GUID values use hex byte text."));
    ui.unsigned_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"((?:0[xX][0-9A-Fa-f]+|[0-9]+))")), ui.value_edit);
    ui.signed_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(-?(?:0[xX][0-9A-Fa-f]+|[0-9]+))")), ui.value_edit);
    auto* real_validator = new QDoubleValidator(ui.value_edit);
    real_validator->setNotation(QDoubleValidator::ScientificNotation);
    ui.real_value_validator = real_validator;
    ui.guid_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9A-Fa-f]{32}")), ui.value_edit);
    edit_layout->addWidget(ui.value_edit, 1);
    ui.apply_value_button = toolbar_button(QStringLiteral("Apply Value"), ui.utf_edit_panel);
    ui.apply_value_button->setEnabled(false);
    edit_layout->addWidget(ui.apply_value_button, 0);
    ui.rename_column_button = toolbar_button(QStringLiteral("Rename Column"), ui.utf_edit_panel);
    ui.rename_column_button->setEnabled(false);
    edit_layout->addWidget(ui.rename_column_button, 0);
    preview_layout->addWidget(ui.utf_edit_panel, 0);

    ui.binary_actions_panel = new QWidget(preview_page);
    auto* binary_layout = new QHBoxLayout(ui.binary_actions_panel);
    binary_layout->setContentsMargins(0, 0, 0, 0);
    ui.replace_binary_button = toolbar_button(QStringLiteral("Replace Binary From File"), ui.binary_actions_panel);
    ui.replace_binary_button->setEnabled(false);
    ui.replace_binary_button->setToolTip(QStringLiteral("For UTF VLData cells, replace this editor session's binary value without mutating the browser-loaded object."));
    binary_layout->addWidget(ui.replace_binary_button, 0);
    binary_layout->addStretch(1);
    preview_layout->addWidget(ui.binary_actions_panel, 0);

    ui.preview_tabs->addTab(preview_page, QStringLiteral("Preview"));
    ui.preview_tabs->addTab(raw_page, QStringLiteral("Raw"));
    ui.preview_tabs->setCurrentIndex(0);
    ui.preview_tabs->setTabEnabled(0, false);
    ui.preview_tabs->setTabEnabled(1, false);
    inspector_splitter->addWidget(ui.preview_tabs);

    ui.log = new QPlainTextEdit(inspector_splitter);
    ui.log->setReadOnly(true);
    ui.log->setMinimumHeight(80);
    ui.log->hide();
    inspector_splitter->addWidget(ui.log);
    inspector_splitter->setSizes({520, 120});
    inspector_layout->addWidget(inspector_splitter, 1);
    QObject::connect(ui.log_toggle_button, &QToolButton::toggled, ui.log, &QPlainTextEdit::setVisible);

    body->addWidget(inspector);
    body->setSizes({980, 420});
    outer->addWidget(body, 1);

    ui.progress = new QProgressBar(parent);
    ui.progress->setRange(0, 0);
    ui.progress->hide();
    outer->addWidget(ui.progress);

    return ui;
}

void refresh_archive_document_ui(
    EditorDocumentUi& ui,
    const ArchiveSessionView& view,
    const DecryptionKeys& keys,
    std::span<const uint8_t> bytes
) {
    ui.table->hide();
    ui.field_table->hide();
    ui.utf_toolbar->hide();
    ui.utf_grid->hide();
    ui.transform_toolbar->hide();
    ui.transform_table->hide();
    ui.schema_table->hide();
    ui.payload_table->hide();
    ui.hex_preview->hide();
    ui.apply_value_button->setEnabled(false);
    ui.rename_column_button->setEnabled(false);
    ui.replace_binary_button->setEnabled(false);
    ui.value_edit->clear();
    ui.utf_edit_panel->hide();
    ui.binary_actions_panel->hide();

    ui.archive_toolbar->show();
    ui.archive_table->show();
    ui.archive_kind_label->setText(archive_kind_name(view.kind));
    const bool is_afs_archive = view.kind == ArchiveKind::Afs;
    ui.rename_archive_entry_button->setVisible(is_afs_archive);
    ui.reserve_afs_id_button->setVisible(is_afs_archive);
    ui.set_afs_timestamp_button->setVisible(is_afs_archive);
    ui.set_archive_wave_id_button->setVisible(view.kind == ArchiveKind::Awb);
    ui.batch_awb_wave_ids_button->setVisible(view.kind == ArchiveKind::Awb);
    ui.archive_entry_options_button->setVisible(view.kind == ArchiveKind::Cpk || view.kind == ArchiveKind::Cvm);
    ui.add_archive_file_button->setText(view.kind == ArchiveKind::Cpk ? QStringLiteral("Add...") : QStringLiteral("Add File"));
    ui.add_archive_file_button->setToolTip(view.kind == ArchiveKind::Cpk
        ? QStringLiteral("Add multiple files or a folder tree while preserving paths relative to the selected folder.")
        : QString{});
    const bool can_reorder_archive = view.kind == ArchiveKind::Afs ||
                                     view.kind == ArchiveKind::Awb ||
                                     view.kind == ArchiveKind::Acx ||
                                     view.kind == ArchiveKind::Cpk ||
                                     view.kind == ArchiveKind::Cvm;
    ui.move_archive_entry_up_button->setVisible(can_reorder_archive);
    ui.move_archive_entry_down_button->setVisible(can_reorder_archive);
    ui.archive_options_button->setVisible(view.kind == ArchiveKind::Afs || view.kind == ArchiveKind::Awb ||
                                          view.kind == ArchiveKind::Cpk || view.kind == ArchiveKind::Cvm);
    ui.import_afs_als_button->setVisible(is_afs_archive);
    ui.export_afs_header_button->setVisible(is_afs_archive);
    ui.import_cvm_script_button->setVisible(view.kind == ArchiveKind::Cvm);
    ui.export_cvm_script_button->setVisible(view.kind == ArchiveKind::Cvm);

    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        modules::afs::populate_editor_archive_table(ui.archive_table, *view.afs);
    } else if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        modules::awb::populate_editor_archive_table(ui.archive_table, *view.awb, keys);
    } else if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        modules::acx::populate_editor_archive_table(ui.archive_table, *view.acx);
    } else if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        modules::cpk::populate_editor_archive_table(ui.archive_table, *view.cpk);
    } else if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        modules::cvm::populate_editor_archive_table(ui.archive_table, *view.cvm);
    }

    for (int col = 0; col < ui.archive_table->columnCount(); ++col) {
        ui.archive_table->resizeColumnToContents(col);
    }
    if (ui.hex_preview != nullptr) {
        constexpr size_t max_preview = 4096;
        const auto count = (std::min)(bytes.size(), max_preview);
        ui.hex_preview->set_bytes(bytes.first(count), bytes.size());
        ui.hex_preview->show();
    }
    if (ui.archive_table->currentRow() < 0 && ui.archive_table->rowCount() > 0) {
        ui.archive_table->setCurrentCell(0, 0);
    }
}

void refresh_transform_document_ui(
    EditorDocumentUi& ui,
    TransformKind kind,
    const TransformSessionView& view,
    const std::vector<modules::TransformDetailRow>& rows,
    QString filter_text,
    std::span<const uint8_t> bytes
) {
    ui.table->hide();
    ui.field_table->hide();
    ui.utf_toolbar->hide();
    ui.utf_grid->hide();
    ui.schema_table->hide();
    ui.apply_value_button->setEnabled(false);
    ui.rename_column_button->setEnabled(false);
    ui.replace_binary_button->setEnabled(false);
    ui.value_edit->clear();
    ui.utf_edit_panel->hide();
    ui.binary_actions_panel->hide();
    ui.archive_toolbar->hide();
    ui.archive_table->hide();

    ui.transform_toolbar->show();
    ui.transform_table->show();
    ui.transform_kind_label->setText(transform_kind_name(kind));
    ui.encode_transform_button->setVisible(kind == TransformKind::AudioEncode ||
                                           kind == TransformKind::Adx ||
                                           kind == TransformKind::Hca);
    ui.decode_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca);
    ui.decrypt_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca);
    ui.encrypt_transform_button->setVisible(kind == TransformKind::Hca);
    ui.rebuild_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca ||
                                            (kind == TransformKind::Aax && view.aax != nullptr) ||
                                            (kind == TransformKind::Aix && view.aix != nullptr) ||
                                            (kind == TransformKind::Sfd && view.sfd != nullptr) ||
                                            (kind == TransformKind::Csb && view.csb != nullptr));
    ui.transform_options_button->setVisible((kind == TransformKind::Adx && view.adx != nullptr) ||
                                            (kind == TransformKind::Hca && view.hca != nullptr));
    ui.extract_transform_button->setVisible((kind == TransformKind::Aax && view.aax != nullptr) ||
                                            (kind == TransformKind::Aix && view.aix != nullptr) ||
                                            (kind == TransformKind::Usm && view.usm != nullptr) ||
                                            (kind == TransformKind::Sfd && view.sfd != nullptr) ||
                                            (kind == TransformKind::Csb && view.csb != nullptr) ||
                                            (kind == TransformKind::Acb && view.acb != nullptr));
    ui.adx_container_build_button->setVisible(kind == TransformKind::Aax || kind == TransformKind::Aix);
    ui.csb_directory_build_button->setVisible(kind == TransformKind::Csb);
    ui.media_build_wizard_button->setVisible(kind == TransformKind::MediaBuild ||
                                             kind == TransformKind::Usm ||
                                             kind == TransformKind::Sfd);
    ui.editor_mux_preview_button->setVisible(
        (kind == TransformKind::Usm && view.usm != nullptr) ||
        (kind == TransformKind::Sfd && view.sfd != nullptr) ||
        (kind == TransformKind::Adx && view.adx != nullptr) ||
        (kind == TransformKind::Hca && view.hca != nullptr) ||
        (kind == TransformKind::Aax && view.aax != nullptr)
    );
    const bool has_acb_awb = kind == TransformKind::Acb && view.acb != nullptr && view.acb->load_awb().has_value();
    ui.open_acb_awb_button->setVisible(kind == TransformKind::Acb);
    ui.export_acb_awb_button->setVisible(kind == TransformKind::Acb);
    ui.open_acb_awb_button->setEnabled(has_acb_awb);
    ui.export_acb_awb_button->setEnabled(has_acb_awb);
    const bool edits_entries = (kind == TransformKind::Aax && view.aax != nullptr) ||
        (kind == TransformKind::Aix && view.aix != nullptr) ||
        (kind == TransformKind::Csb && view.csb != nullptr);
    ui.add_transform_entry_button->setVisible(edits_entries);
    ui.replace_transform_entry_button->setVisible(edits_entries);
    ui.remove_transform_entry_button->setVisible(edits_entries);
    ui.move_transform_entry_up_button->setVisible(edits_entries);
    ui.move_transform_entry_down_button->setVisible(edits_entries);
    ui.rename_transform_entry_button->setVisible(kind == TransformKind::Csb && view.csb != nullptr);
    ui.toggle_transform_entry_flag_button->setVisible(
        (kind == TransformKind::Aax && view.aax != nullptr) ||
        (kind == TransformKind::Csb && view.csb != nullptr));
    ui.add_transform_entry_button->setText(
        kind == TransformKind::Aix ? QStringLiteral("Add...") : QStringLiteral("Add"));
    ui.add_transform_entry_button->setToolTip(kind == TransformKind::Aix
        ? QStringLiteral("Add a complete segment or a complete layer of ADX streams.")
        : QString{});
    ui.toggle_transform_entry_flag_button->setText(
        kind == TransformKind::Aax ? QStringLiteral("Toggle Loop") : QStringLiteral("Toggle Streamed"));
    ui.transform_filter_edit->setVisible(kind == TransformKind::Usm);
    ui.transform_filter_edit->setPlaceholderText(
        kind == TransformKind::Usm
            ? QStringLiteral("Filter rows: sfv, sfa, sbt, ch 0, utf...")
            : QStringLiteral("Filter rows")
    );

    std::vector<modules::TransformDetailRow> filtered_rows;
    filtered_rows.reserve(rows.size());
    for (const auto& detail : rows) {
        if (matches_transform_filter(detail, filter_text)) {
            filtered_rows.push_back(detail);
        }
    }
    ui.transform_model->set_rows(std::move(filtered_rows));
    if (ui.transform_table->horizontalScrollBar() != nullptr) {
        ui.transform_table->horizontalScrollBar()->setValue(0);
    }
    if (ui.hex_preview != nullptr) {
        constexpr size_t max_preview = 4096;
        const auto count = (std::min)(bytes.size(), max_preview);
        ui.hex_preview->set_bytes(bytes.first(count), bytes.size());
        ui.hex_preview->show();
    }
    if (!ui.transform_table->currentIndex().isValid() && ui.transform_model->rowCount() > 0) {
        ui.transform_table->setCurrentIndex(ui.transform_model->index(0, 0));
        ui.transform_table->selectRow(0);
    }
}

void refresh_document_info_ui(EditorDocumentUi& ui, QWidget* parent, const EditorDocumentInfoView& view) {
    while (auto* item = ui.info_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const auto all_rows = document_info_rows(view);
    std::vector<InfoRow> rows;
    rows.reserve(6);
    static constexpr std::array<std::string_view, 6> header_fields{
        "Session", "Format", "Bytes", "Source path", "Validation", "Inspector kind"
    };
    for (const auto field : header_fields) {
        const auto found = std::ranges::find(all_rows, field, &InfoRow::name);
        if (found != all_rows.end()) {
            rows.push_back(*found);
        }
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto column = (i % 2) * 2;
        const auto row = i / 2;
        ui.info_grid->addWidget(dim_label(utf8_to_qstring(rows[static_cast<size_t>(i)].name), parent), row, column, Qt::AlignVCenter);
        ui.info_grid->addWidget(value_label(utf8_to_qstring(rows[static_cast<size_t>(i)].value), parent), row, column + 1, Qt::AlignVCenter);
    }
}

} // namespace cristudio
