#include "editor/archive_editor_helpers.hpp"

#include "modules/afs/afs_edit.hpp"
#include "modules/afs/afs_edit_ui.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/awb/awb_edit_ui.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/cpk/cpk_edit_ui.hpp"
#include "modules/cvm/cvm_edit.hpp"
#include "modules/cvm/cvm_edit_ui.hpp"
#include "modules/utf/utf_edit_ui.hpp"
#include "path_text.hpp"

#include "acx_container.hpp"
#include "afs_container.hpp"
#include "cpk_container.hpp"

#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace cristudio {

int validated_archive_index(const ArchiveSessionView& view, int row) {
    if (row < 0) {
        return -1;
    }
    switch (view.kind) {
    case ArchiveKind::Afs:
        return view.afs != nullptr && row < static_cast<int>(view.afs->entry_count()) ? row : -1;
    case ArchiveKind::Awb:
        return view.awb != nullptr && row < static_cast<int>(view.awb->file_count()) ? row : -1;
    case ArchiveKind::Acx:
        return view.acx != nullptr && row < static_cast<int>(view.acx->entry_count()) ? row : -1;
    case ArchiveKind::Cpk:
        return view.cpk != nullptr && row < static_cast<int>(view.cpk->file_count()) ? row : -1;
    case ArchiveKind::Cvm:
        return view.cvm != nullptr && row < static_cast<int>(view.cvm->entry_count()) ? row : -1;
    case ArchiveKind::None:
        break;
    }
    return -1;
}

ArchiveItemEditResult edit_archive_table_item(
    const MutableArchiveSessionView& view,
    int row,
    int column,
    const QString& text
) {
    if (row < 0) {
        return {};
    }

    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        const auto index = static_cast<uint32_t>(row);
        std::expected<void, std::string> result{};
        if (column == 2) {
            const auto name = qstring_to_utf8(text.trimmed());
            result = modules::afs::rename_file(*view.afs, index, name.empty() ? std::nullopt : std::optional<std::string>(name));
        } else if (column == 6) {
            const auto name = qstring_to_utf8(text.trimmed());
            result = modules::afs::set_header_source_name(*view.afs, index, name.empty() ? std::nullopt : std::optional<std::string>(name));
        } else if (column == 8) {
            auto bytes = modules::utf::parse_hex_bytes(text, 12);
            if (!bytes) {
                return {
                    .handled = true,
                    .warning_title = QStringLiteral("AFS metadata edit failed"),
                    .error = bytes.error()
                };
            }
            std::array<uint8_t, 12> metadata{};
            std::copy(bytes->begin(), bytes->end(), metadata.begin());
            result = modules::afs::set_directory_metadata(*view.afs, index, metadata);
        } else {
            return {};
        }
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("AFS edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Changed AFS entry %1.").arg(row)
        };
    }

    if (view.kind == ArchiveKind::Awb && view.awb != nullptr && column == 1) {
        bool ok = false;
        const auto wave_id = text.toULongLong(&ok, 0);
        if (!ok) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("AWB edit failed"),
                .error = QStringLiteral("Wave ID must be an unsigned integer.")
            };
        }
        auto result = modules::awb::set_wave_id(*view.awb, static_cast<uint32_t>(row), wave_id);
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("AWB edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Changed AWB wave ID at index %1.").arg(row)
        };
    }

    if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        const auto index = static_cast<size_t>(row);
        std::expected<void, std::string> result{};
        if (column == 1) {
            result = modules::cpk::rename_file(*view.cpk, index, qstring_to_utf8(text.trimmed()));
        } else if (column == 2) {
            result = modules::cpk::set_dirname(*view.cpk, index, qstring_to_utf8(text.trimmed()));
        } else if (column == 4) {
            result = modules::cpk::set_filename(*view.cpk, index, qstring_to_utf8(text.trimmed()));
        } else if (column == 6) {
            bool ok = false;
            const auto id = text.toUInt(&ok, 0);
            if (!ok) {
                return {
                    .handled = true,
                    .warning_title = QStringLiteral("CPK edit failed"),
                    .error = QStringLiteral("ID must be an unsigned integer.")
                };
            }
            result = modules::cpk::set_entry_id(*view.cpk, index, id);
        } else if (column == 12) {
            const auto normalized = text.trimmed().toLower();
            if (normalized == QStringLiteral("yes") || normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) {
                result = modules::cpk::set_request_compress(*view.cpk, index, true);
            } else if (normalized == QStringLiteral("no") || normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) {
                result = modules::cpk::set_request_compress(*view.cpk, index, false);
            } else {
                return {
                    .handled = true,
                    .warning_title = QStringLiteral("CPK edit failed"),
                    .error = QStringLiteral("Compress On Save must be yes or no.")
                };
            }
        } else if (column == 13) {
            result = modules::cpk::set_group(*view.cpk, index, qstring_to_utf8(text));
        } else if (column == 14) {
            result = modules::cpk::set_attribute(*view.cpk, index, qstring_to_utf8(text));
        } else if (column == 15) {
            result = modules::cpk::set_user_string(*view.cpk, index, qstring_to_utf8(text));
        } else if (column == 16) {
            bool ok = false;
            const auto update_time = text.toULongLong(&ok, 0);
            if (!ok) {
                return {
                    .handled = true,
                    .warning_title = QStringLiteral("CPK edit failed"),
                    .error = QStringLiteral("Update Date must be an unsigned integer.")
                };
            }
            result = modules::cpk::set_update_date_time(*view.cpk, index, update_time);
        } else {
            return {};
        }
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("CPK edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Changed CPK entry %1.").arg(row)
        };
    }

    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr && column == 1) {
        auto result = modules::cvm::rename_file(*view.cvm, static_cast<uint32_t>(row), path_from_qstring(text.trimmed()));
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("CVM rename failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Renamed CVM entry %1.").arg(row)
        };
    }

    return {};
}

ArchiveBuildResult build_archive_session_bytes(
    const MutableArchiveSessionView& view
) {
    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        auto built = modules::afs::build_session_bytes(*view.afs);
        if (!built) {
            return {
                .handled = true,
                .log_message = QStringLiteral("AFS build failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QStringLiteral("Build failed"),
                .error = utf8_to_qstring(built.error())
            };
        }
        const auto byte_count = built->size();
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QStringLiteral("Built AFS session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
        };
    }

    if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        auto built = modules::awb::build_session_bytes(*view.awb);
        if (!built) {
            return {
                .handled = true,
                .log_message = QStringLiteral("AWB build failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QStringLiteral("Build failed"),
                .error = utf8_to_qstring(built.error())
            };
        }
        const auto byte_count = built->size();
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QStringLiteral("Built AWB session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
        };
    }

    if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        auto built = modules::acx::rebuild_session_bytes(*view.acx);
        if (!built) {
            return {
                .handled = true,
                .log_message = QStringLiteral("ACX rebuild failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QStringLiteral("Build failed"),
                .error = utf8_to_qstring(built.error())
            };
        }
        const auto byte_count = built->size();
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QStringLiteral("Rebuilt ACX session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
        };
    }

    if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        const bool obfuscate_utf = view.cpk_obfuscate_utf != nullptr && *view.cpk_obfuscate_utf;
        auto built = modules::cpk::build_session_bytes(*view.cpk, obfuscate_utf);
        if (!built) {
            return {
                .handled = true,
                .log_message = QStringLiteral("CPK save failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QStringLiteral("Build failed"),
                .error = utf8_to_qstring(built.error())
            };
        }
        const auto byte_count = built->size();
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QStringLiteral("Built CPK session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
        };
    }

    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        auto built = modules::cvm::save_session_bytes(*view.cvm);
        if (!built) {
            return {
                .handled = true,
                .log_message = QStringLiteral("CVM save failed: %1").arg(utf8_to_qstring(built.error())),
                .warning_title = QStringLiteral("Build failed"),
                .error = utf8_to_qstring(built.error())
            };
        }
        const auto byte_count = built->size();
        return {
            .handled = true,
            .bytes = std::move(*built),
            .log_message = QStringLiteral("Built CVM session bytes: %1 bytes").arg(static_cast<qulonglong>(byte_count))
        };
    }

    return {};
}

ArchiveItemEditResult edit_archive_entry_properties(
    QWidget* parent,
    const MutableArchiveSessionView& view,
    int row
) {
    if (row < 0) {
        return {};
    }

    if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        const auto index = static_cast<size_t>(row);
        auto* entry = view.cpk->try_file(index);
        if (entry == nullptr) {
            return {};
        }

        auto selected = modules::cpk::choose_entry_properties(parent, *entry);
        if (!selected) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("CPK edit failed"),
                .error = selected.error()
            };
        }
        if (!*selected) {
            return {.handled = true};
        }

        const auto& options = **selected;
        const auto original_full_path = entry->full_path().generic_string();
        const auto original_dirname = entry->dirname;
        if (options.full_path != original_full_path) {
            auto renamed = modules::cpk::rename_file(*view.cpk, index, options.full_path);
            if (!renamed) {
                return {
                    .handled = true,
                    .warning_title = QStringLiteral("CPK rename failed"),
                    .error = utf8_to_qstring(renamed.error())
                };
            }
        }
        if (options.dirname != original_dirname) {
            if (auto result = modules::cpk::set_dirname(*view.cpk, index, options.dirname); !result) {
                return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
            }
        }
        if (auto result = modules::cpk::set_entry_id(*view.cpk, index, options.id); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        if (auto result = modules::cpk::set_request_compress(*view.cpk, index, options.request_compress); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        if (auto result = modules::cpk::set_group(*view.cpk, index, options.group); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        if (auto result = modules::cpk::set_attribute(*view.cpk, index, options.attribute); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        if (auto result = modules::cpk::set_user_string(*view.cpk, index, options.user_string); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        if (auto result = modules::cpk::set_update_date_time(*view.cpk, index, options.update_date_time); !result) {
            return {.handled = true, .warning_title = QStringLiteral("CPK edit failed"), .error = utf8_to_qstring(result.error())};
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Updated CPK entry %1 properties.").arg(row)
        };
    }

    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        auto path = modules::cvm::choose_entry_path(parent, view.cvm->entry(static_cast<uint32_t>(row)));
        if (!path) {
            return {.handled = true};
        }
        auto result = modules::cvm::rename_file(*view.cvm, static_cast<uint32_t>(row), *path);
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("CVM rename failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Renamed CVM entry %1.").arg(row)
        };
    }

    return {};
}

ArchiveItemEditResult edit_archive_options(
    QWidget* parent,
    const MutableArchiveSessionView& view
) {
    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        auto options = modules::afs::choose_build_options(parent, *view.afs);
        if (!options) {
            return {.handled = true};
        }
        auto result = modules::afs::set_build_options(
            *view.afs,
            options->alignment,
            options->directory_table_enabled,
            options->first_payload_offset
        );
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("AFS options failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Updated AFS build options.")};
    }

    if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        auto options = modules::awb::choose_build_options(parent, *view.awb);
        if (!options) {
            return {.handled = true};
        }
        auto result = modules::awb::set_build_options(
            *view.awb,
            options->version,
            options->alignment,
            options->subkey,
            options->id_size,
            options->offset_size
        );
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("AWB options failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Updated AWB build options.")};
    }

    if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        auto selection = modules::cpk::choose_build_options(
            parent,
            *view.cpk,
            view.cpk_obfuscate_utf != nullptr && *view.cpk_obfuscate_utf);
        if (!selection) {
            return {.handled = true};
        }
        modules::cpk::set_options(*view.cpk, std::move(selection->options));
        if (selection->compress_all.has_value()) {
            modules::cpk::set_all_request_compress(*view.cpk, *selection->compress_all);
        }
        if (view.cpk_obfuscate_utf != nullptr) {
            *view.cpk_obfuscate_utf = selection->obfuscate_utf;
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Updated CPK build options.")};
    }

    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        auto options = modules::cvm::choose_metadata_options(parent, *view.cvm);
        if (!options) {
            return {.handled = true};
        }
        auto result = modules::cvm::set_metadata_options(*view.cvm, *options);
        if (!result) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("CVM metadata failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Updated CVM metadata options.")};
    }

    return {};
}

ArchiveItemEditResult add_archive_file(QWidget* parent, const MutableArchiveSessionView& view) {
    if (view.kind == ArchiveKind::None) {
        return {};
    }

    if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        QMessageBox choice(parent);
        choice.setWindowTitle(QStringLiteral("Add to CPK"));
        choice.setText(QStringLiteral("Add files or import a folder tree?"));
        auto* files_button = choice.addButton(QStringLiteral("Add Files..."), QMessageBox::ActionRole);
        auto* directory_button = choice.addButton(QStringLiteral("Add Folder..."), QMessageBox::ActionRole);
        choice.addButton(QMessageBox::Cancel);
        choice.exec();

        const auto original_count = view.cpk->file_count();
        std::expected<size_t, std::string> added = size_t{0};
        if (choice.clickedButton() == files_button) {
            const auto paths = QFileDialog::getOpenFileNames(parent, QStringLiteral("Choose files to add"));
            if (paths.isEmpty()) {
                return {.handled = true};
            }
            std::vector<modules::cpk::AddFileSource> sources;
            sources.reserve(static_cast<size_t>(paths.size()));
            for (const auto& path_text : paths) {
                const auto path = path_from_qstring(path_text);
                sources.push_back({
                    .local_path = path,
                    .archive_path = path.filename().generic_string(),
                });
            }
            added = modules::cpk::add_files(*view.cpk, sources);
        } else if (choice.clickedButton() == directory_button) {
            const auto path_text = QFileDialog::getExistingDirectory(parent, QStringLiteral("Choose folder to add"));
            if (path_text.isEmpty()) {
                return {.handled = true};
            }
            added = modules::cpk::add_directory(*view.cpk, path_from_qstring(path_text));
        } else {
            return {.handled = true};
        }

        if (!added) {
            return {.handled = true, .warning_title = QStringLiteral("Add failed"), .error = utf8_to_qstring(added.error())};
        }
        if (*added == 0) {
            return {
                .handled = true,
                .warning_title = QStringLiteral("Nothing added"),
                .error = QStringLiteral("The selected folder contains no regular files.")
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Added %1 CPK file(s).").arg(static_cast<qulonglong>(*added)),
            .selected_row = static_cast<int>(original_count)
        };
    }

    const auto path_text = QFileDialog::getOpenFileName(parent, QStringLiteral("Choose file to add"));
    if (path_text.isEmpty()) {
        return {.handled = true};
    }
    auto bytes = read_file_bytes(path_from_qstring(path_text));
    if (!bytes) {
        return {.handled = true, .warning_title = QStringLiteral("Add failed"), .error = bytes.error()};
    }

    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        auto options = modules::afs::choose_add_file_options(parent, *view.afs, path_text);
        if (!options) {
            return {.handled = true};
        }
        auto result = modules::afs::add_file(
            *view.afs,
            options->file_id,
            *bytes,
            std::move(options->name),
            options->directory_metadata,
            std::move(options->header_source_name)
        );
        if (!result) {
            return {.handled = true, .warning_title = QStringLiteral("Add failed"), .error = utf8_to_qstring(result.error())};
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Added AFS file %1 at file ID %2.").arg(path_text).arg(options->file_id)
        };
    }

    if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        const auto wave_id = modules::awb::add_file(*view.awb, *bytes);
        return {
            .handled = true,
            .changed = true,
            .change_message = QStringLiteral("Added AWB file %1 as wave ID %2.").arg(path_text).arg(static_cast<qulonglong>(wave_id))
        };
    }

    if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        auto result = modules::acx::add_file(*view.acx, *bytes);
        if (!result) {
            return {.handled = true, .warning_title = QStringLiteral("Add failed"), .error = utf8_to_qstring(result.error())};
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Added ACX file %1.").arg(path_text)};
    }

    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        bool ok = false;
        const auto default_path = path_from_qstring(path_text).filename().generic_string();
        const auto archive_path = QInputDialog::getText(
            parent,
            QStringLiteral("Add CVM file"),
            QStringLiteral("ROFS path"),
            QLineEdit::Normal,
            utf8_to_qstring(default_path),
            &ok
        );
        if (!ok || archive_path.trimmed().isEmpty()) {
            return {.handled = true};
        }
        auto result = modules::cvm::add_bytes(*view.cvm, *bytes, path_from_qstring(archive_path.trimmed()));
        if (!result) {
            return {.handled = true, .warning_title = QStringLiteral("Add failed"), .error = utf8_to_qstring(result.error())};
        }
        return {.handled = true, .changed = true, .change_message = QStringLiteral("Added CVM file %1.").arg(archive_path)};
    }

    return {};
}

ArchiveItemEditResult replace_archive_file(QWidget* parent, const MutableArchiveSessionView& view, int index) {
    if (index < 0) {
        return {};
    }
    const auto path_text = QFileDialog::getOpenFileName(parent, QStringLiteral("Choose replacement file"));
    if (path_text.isEmpty()) {
        return {.handled = true};
    }
    auto bytes = read_file_bytes(path_from_qstring(path_text));
    if (!bytes) {
        return {.handled = true, .warning_title = QStringLiteral("Replacement failed"), .error = bytes.error()};
    }

    std::expected<void, std::string> result{};
    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        result = modules::afs::replace_file(*view.afs, static_cast<uint32_t>(index), *bytes);
    } else if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        result = modules::awb::replace_file(*view.awb, static_cast<uint32_t>(index), *bytes);
    } else if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        result = modules::acx::replace_file(*view.acx, static_cast<uint32_t>(index), *bytes);
    } else if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        result = modules::cpk::replace_bytes(*view.cpk, static_cast<size_t>(index), *bytes);
    } else if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        result = modules::cvm::replace_bytes(*view.cvm, static_cast<uint32_t>(index), *bytes);
    } else {
        return {};
    }
    if (!result) {
        return {.handled = true, .warning_title = QStringLiteral("Replacement failed"), .error = utf8_to_qstring(result.error())};
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QStringLiteral("Replaced archive entry %1 from %2.").arg(index).arg(path_text),
        .selected_row = index
    };
}

ArchiveItemEditResult remove_archive_file(QWidget* parent, const MutableArchiveSessionView& view, int index) {
    if (index < 0) {
        return {};
    }
    if (QMessageBox::question(parent, QStringLiteral("Remove archive entry"), QStringLiteral("Remove entry %1?").arg(index)) != QMessageBox::Yes) {
        return {.handled = true};
    }

    std::expected<void, std::string> result{};
    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        result = modules::afs::remove_file(*view.afs, static_cast<uint32_t>(index));
    } else if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        result = modules::awb::remove_file(*view.awb, static_cast<uint32_t>(index));
    } else if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        result = modules::acx::remove_file(*view.acx, static_cast<uint32_t>(index));
    } else if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        result = modules::cpk::remove_file(*view.cpk, static_cast<size_t>(index));
    } else if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        result = modules::cvm::remove_file(*view.cvm, static_cast<uint32_t>(index));
    } else {
        return {};
    }
    if (!result) {
        return {.handled = true, .warning_title = QStringLiteral("Remove failed"), .error = utf8_to_qstring(result.error())};
    }
    return {.handled = true, .changed = true, .change_message = QStringLiteral("Removed archive entry %1.").arg(index)};
}

ArchiveItemEditResult move_archive_entry(const MutableArchiveSessionView& view, int index, int delta) {
    if (index < 0 || delta == 0) {
        return {};
    }
    const auto target = index + delta;
    if (target < 0) {
        return {};
    }

    std::expected<void, std::string> result{};
    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        if (target >= static_cast<int>(view.afs->entry_count())) return {};
        result = modules::afs::move_file(*view.afs, static_cast<uint32_t>(index), static_cast<uint32_t>(target));
    } else if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        if (target >= static_cast<int>(view.awb->file_count())) return {};
        result = modules::awb::move_file(*view.awb, static_cast<uint32_t>(index), static_cast<uint32_t>(target));
    } else if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        if (target >= static_cast<int>(view.acx->entry_count())) return {};
        result = modules::acx::move_file(*view.acx, static_cast<uint32_t>(index), static_cast<uint32_t>(target));
    } else if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        if (target >= static_cast<int>(view.cpk->file_count())) return {};
        result = modules::cpk::move_file(*view.cpk, static_cast<size_t>(index), static_cast<size_t>(target));
    } else if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        if (target >= static_cast<int>(view.cvm->entry_count())) return {};
        result = modules::cvm::move_file(*view.cvm, static_cast<uint32_t>(index), static_cast<uint32_t>(target));
    } else {
        return {};
    }
    if (!result) {
        return {.handled = true, .warning_title = QStringLiteral("Move failed"), .error = utf8_to_qstring(result.error())};
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QStringLiteral("Moved archive entry %1 to %2.").arg(index).arg(target),
        .selected_row = target
    };
}

QString archive_entry_default_name(const ArchiveSessionView& view, uint32_t index) {
    switch (view.kind) {
    case ArchiveKind::Afs:
        if (view.afs != nullptr) {
            return path_to_qstring(view.afs->entry(index).suggested_path());
        }
        break;
    case ArchiveKind::Awb:
        if (view.awb != nullptr) {
            auto suggested = view.awb->suggested_path(index);
            if (suggested) {
                return path_to_qstring(*suggested);
            }
        }
        break;
    case ArchiveKind::Acx:
        if (view.acx != nullptr) {
            return path_to_qstring(modules::acx::suggested_path(*view.acx, index));
        }
        break;
    case ArchiveKind::Cpk:
        if (view.cpk != nullptr) {
            return path_to_qstring(view.cpk->files()[static_cast<size_t>(index)].full_path());
        }
        break;
    case ArchiveKind::Cvm:
        if (view.cvm != nullptr) {
            return path_to_qstring(view.cvm->entry(index).path);
        }
        break;
    case ArchiveKind::None:
        break;
    }
    return QStringLiteral("entry_%1.bin").arg(index);
}

std::expected<std::span<const uint8_t>, QString> archive_entry_bytes(
    const ArchiveSessionView& view,
    uint32_t index,
    std::vector<uint8_t>& scratch
) {
    switch (view.kind) {
    case ArchiveKind::Afs:
        if (view.afs != nullptr) {
            auto data = view.afs->file_data(index);
            if (!data) {
                return std::unexpected(utf8_to_qstring(data.error()));
            }
            return *data;
        }
        break;
    case ArchiveKind::Awb:
        if (view.awb != nullptr) {
            auto data = view.awb->file_data(index);
            if (!data) {
                return std::unexpected(utf8_to_qstring(data.error()));
            }
            return *data;
        }
        break;
    case ArchiveKind::Acx:
        if (view.acx != nullptr) {
            auto data = view.acx->file_data(index);
            if (!data) {
                return std::unexpected(utf8_to_qstring(data.error()));
            }
            return *data;
        }
        break;
    case ArchiveKind::Cpk:
        if (view.cpk != nullptr) {
            auto data = view.cpk->file_bytes(index);
            if (!data) {
                return std::unexpected(utf8_to_qstring(data.error()));
            }
            scratch = std::move(*data);
            return std::span<const uint8_t>(scratch.data(), scratch.size());
        }
        break;
    case ArchiveKind::Cvm:
        if (view.cvm != nullptr) {
            auto data = view.cvm->file_data(index);
            if (!data) {
                return std::unexpected(utf8_to_qstring(data.error()));
            }
            return *data;
        }
        break;
    case ArchiveKind::None:
        break;
    }
    return std::unexpected(QStringLiteral("No archive entry is selected"));
}

QString archive_entry_preview_text(
    const ArchiveSessionView& view,
    uint32_t index,
    std::span<const uint8_t> bytes
) {
    if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        return modules::cvm::entry_preview(*view.cvm, index, bytes);
    }
    return hex_preview(bytes);
}

} // namespace cristudio
