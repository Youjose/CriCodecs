#include "main_window.hpp"

#include "path_text.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>
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

bool extraction_message_is_failure_local(const std::string& message) {
    auto lower = QString::fromStdString(message).toLower();
    return lower.contains(QStringLiteral("failed")) ||
           lower.contains(QStringLiteral("error")) ||
           lower.contains(QStringLiteral("could not")) ||
           lower.contains(QStringLiteral("missing")) ||
           lower.contains(QStringLiteral("needs "));
}

QString to_qstring_local(const std::filesystem::path& path) {
    return path_to_qstring(path);
}

QString archive_basename_local(QString text) {
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }
    const auto slash = text.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 && slash + 1 < text.size() ? text.mid(slash + 1) : text;
}

QString strip_mux_prefix_local(QString text) {
    constexpr auto prefix = "Mux preview/";
    return text.startsWith(QLatin1String(prefix))
        ? text.mid(static_cast<int>(std::char_traits<char>::length(prefix)))
        : text;
}

QString extraction_target_label_local(const ExtractionTarget& target) {
    switch (target.kind) {
    case ExtractionTarget::Kind::Document:
        return archive_basename_local(utf8_to_qstring(target.document.display_name.empty()
            ? target.document.path.filename().generic_string()
            : target.document.display_name));
    case ExtractionTarget::Kind::Entry:
        return archive_basename_local(strip_mux_prefix_local(utf8_to_qstring(target.entry.name.empty()
            ? target.entry.type
            : target.entry.name)));
    }
    return QStringLiteral("(unknown)");
}

QString extraction_plan_text_local(
    const std::vector<ExtractionTarget>& targets,
    ExtractionMode mode,
    const QString& output_dir,
    bool include_mux_outputs
) {
    size_t document_count = 0;
    size_t entry_count = 0;
    size_t archive_entry_count = 0;
    for (const auto& target : targets) {
        if (target.kind == ExtractionTarget::Kind::Document) {
            ++document_count;
            archive_entry_count += target.document.entries.size();
        } else {
            ++entry_count;
        }
    }

    QStringList lines;
    lines << QStringLiteral("Output: %1").arg(output_dir);
    lines << QStringLiteral("Mode: %1").arg(mode == ExtractionMode::Raw ? QStringLiteral("raw") : QStringLiteral("decoded"));
    lines << QStringLiteral("Targets: %1 documents, %2 entries").arg(document_count).arg(entry_count);
    if (archive_entry_count != 0) {
        lines << QStringLiteral("Archive entries queued by selected documents: %1").arg(archive_entry_count);
    }
    lines << QStringLiteral("USM/SFD mux outputs: %1").arg(include_mux_outputs ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QString{};
    lines << QStringLiteral("First targets:");
    const auto shown = std::min<size_t>(targets.size(), 12);
    for (size_t i = 0; i < shown; ++i) {
        lines << QStringLiteral("  %1. %2").arg(i + 1).arg(extraction_target_label_local(targets[i]));
    }
    if (targets.size() > shown) {
        lines << QStringLiteral("  ... %1 more").arg(targets.size() - shown);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

void MainWindow::add_paths(const QList<QUrl>& urls) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(urls.size());
    for (const auto& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        paths.emplace_back(path_from_qstring(url.toLocalFile()));
    }
    start_loading_paths(std::move(paths));
}

void MainWindow::add_path(const std::filesystem::path& path) {
    start_loading_paths({path});
}

void MainWindow::add_directory(const std::filesystem::path& path) {
    start_loading_paths({path});
}

void MainWindow::start_loading_paths(std::vector<std::filesystem::path> paths) {
    if (paths.empty()) {
        return;
    }
    if (m_load_running) {
        m_queued_load_paths.insert(
            m_queued_load_paths.end(),
            std::make_move_iterator(paths.begin()),
            std::make_move_iterator(paths.end())
        );
        statusBar()->showMessage(QStringLiteral("Queued files for loading"), 3000);
        return;
    }

    auto progress = std::make_shared<LoadProgress>();
    m_load_progress = progress;
    m_loading_status_label->setText(QStringLiteral("0 checked · 0 valid · 0 rejected"));
    m_loading_status_label->show();
    m_loading_bar->show();
    statusBar()->showMessage(QStringLiteral("Loading assets..."));
    m_load_running = true;
    auto keys = m_decryption_keys;
    m_load_watcher->setFuture(QtConcurrent::run([paths = std::move(paths), progress, keys = std::move(keys)]() mutable {
        LoadResult result;

        auto process_file = [&result, progress, &keys](const std::filesystem::path& file_path) {
            ++result.candidate_count;
            progress->candidate_count.fetch_add(1, std::memory_order_relaxed);
            std::error_code canonical_error;
            const auto canonical = std::filesystem::weakly_canonical(file_path, canonical_error);
            if (canonical_error) {
                ++result.rejected_count;
                progress->rejected_count.fetch_add(1, std::memory_order_relaxed);
                result.log_messages.push_back(QStringLiteral("Rejected path: ") + to_qstring_local(file_path));
                return;
            }

            std::string reason;
            auto document = probe_document_summary(canonical, reason);
            if (!document) {
                ++result.rejected_count;
                progress->rejected_count.fetch_add(1, std::memory_order_relaxed);
                result.log_messages.push_back(
                    QStringLiteral("Discarded invalid file: ") + to_qstring_local(file_path) +
                    QStringLiteral(" (") + utf8_to_qstring(reason) + QStringLiteral(")")
                );
                return;
            }
            progress->valid_count.fetch_add(1, std::memory_order_relaxed);
            result.loaded.emplace_back(std::move(*document), to_qstring_local(canonical));
        };

        for (const auto& path : paths) {
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec)) {
                for (std::filesystem::recursive_directory_iterator it(
                         path,
                         std::filesystem::directory_options::skip_permission_denied,
                         ec
                     ), end;
                     it != end;
                     it.increment(ec)) {
                    if (ec) {
                        result.log_messages.push_back(
                            QStringLiteral("Skipped directory entry under ") + to_qstring_local(path) +
                            QStringLiteral(": ") + QString::fromStdString(ec.message())
                        );
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec)) {
                        process_file(it->path());
                    }
                    ec.clear();
                }
            } else if (std::filesystem::is_regular_file(path, ec)) {
                process_file(path);
            } else {
                result.log_messages.push_back(QStringLiteral("Rejected path: ") + to_qstring_local(path));
            }
        }

        return result;
    }));
    m_work_timer->start(50);
}

void MainWindow::start_extraction(std::vector<ExtractionTarget> targets, ExtractionMode mode, std::optional<int> mux_audio_choice) {
    if (targets.empty()) {
        statusBar()->showMessage(QStringLiteral("Nothing selected to extract"), 3000);
        return;
    }
    if (m_extract_running) {
        statusBar()->showMessage(QStringLiteral("Extraction is already running"), 3000);
        return;
    }

    for (auto& target : targets) {
        if (target.kind != ExtractionTarget::Kind::Document || target.document.summary_loaded) {
            continue;
        }
        std::string reason;
        auto loaded = materialize_document_summary(target.document, reason, m_decryption_keys);
        if (!loaded) {
            statusBar()->showMessage(
                QStringLiteral("Extraction blocked: could not load document details for %1")
                    .arg(utf8_to_qstring(target.document.display_name)),
                5000
            );
            append_log(
                QStringLiteral("Extraction blocked: ") +
                utf8_to_qstring(target.document.display_name) +
                QStringLiteral(" (") + utf8_to_qstring(reason) + QStringLiteral(")")
            );
            return;
        }
        target.document = std::move(*loaded);
    }

    const auto output_dir_text = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose extraction folder"));
    if (output_dir_text.isEmpty()) {
        return;
    }
    const auto plan = extraction_plan_text_local(targets, mode, output_dir_text, m_allow_mux_extract_outputs);
    const auto answer = QMessageBox::question(
        this,
        mode == ExtractionMode::Raw ? QStringLiteral("Raw Extraction Plan") : QStringLiteral("Extraction Plan"),
        plan,
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Ok
    );
    if (answer != QMessageBox::Ok) {
        return;
    }

    auto progress = std::make_shared<ExtractionProgress>();
    progress->target_count.store(targets.size(), std::memory_order_relaxed);
    m_extract_progress = progress;
    m_loading_status_label->setText(QStringLiteral("0/%1 targets · 0 extracted · 0 failed").arg(targets.size()));
    m_loading_status_label->show();
    m_loading_bar->show();
    statusBar()->showMessage(mode == ExtractionMode::Raw ? QStringLiteral("Raw extraction running...") : QStringLiteral("Extraction running..."));
    m_extract_running = true;
    const auto keys = m_decryption_keys;
    ExtractionOptions options;
    options.include_mux_outputs = m_allow_mux_extract_outputs;
    options.mux_audio_choice = mux_audio_choice.value_or(0);
    if (options.include_mux_outputs) {
        options.ffmpeg_path = path_from_qstring(ffmpeg_executable_path());
    }
    const auto output_dir = path_from_qstring(output_dir_text);
    const auto live_log_path = log_path();
    append_log(
        (mode == ExtractionMode::Raw ? QStringLiteral("Raw extraction started: ") : QStringLiteral("Extraction started: ")) +
        output_dir_text +
        QStringLiteral(" (%1 selected targets)").arg(targets.size())
    );
    m_extract_watcher->setFuture(QtConcurrent::run(
        [targets = std::move(targets), output_dir, mode, keys, options, progress, live_log_path]() mutable {
            QFile live_log(live_log_path);
            const bool live_log_open = live_log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
            auto write_live_log = [&live_log, live_log_open](const QString& message) {
                if (!live_log_open) {
                    return;
                }
                QTextStream stream(&live_log);
                stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << " " << message << "\n";
                stream.flush();
                live_log.flush();
            };

            auto worker_options = options;
            worker_options.event_callback = [progress, &write_live_log](const ExtractionEvent& event) {
                if (event.processed_delta != 0) {
                    progress->processed_count.fetch_add(event.processed_delta, std::memory_order_relaxed);
                }
                if (event.extracted_delta != 0) {
                    progress->extracted_count.fetch_add(event.extracted_delta, std::memory_order_relaxed);
                }
                if (event.failed_delta != 0) {
                    progress->failed_count.fetch_add(event.failed_delta, std::memory_order_relaxed);
                }
                if (!event.message.empty()) {
                    write_live_log(utf8_to_qstring(event.message));
                }
            };

            auto combined = extract_targets(targets, output_dir, mode, keys, worker_options);
            combined.messages_logged_live = live_log_open;
            progress->processed_count.store(targets.size(), std::memory_order_relaxed);
            const auto report_name = QStringLiteral("cristudio_extraction_report_%1.txt")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ")));
            const auto report_path = output_dir / qt_to_utf8_local(report_name);
            QFile report_file(path_to_qstring(report_path));
            if (report_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream stream(&report_file);
                stream << "CriStudio extraction report\n";
                stream << "Time: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
                stream << "Mode: " << (mode == ExtractionMode::Raw ? "raw" : "decoded") << "\n";
                stream << "Output: " << path_to_qstring(output_dir) << "\n";
                stream << "Summary: " << combined.extracted << " extracted, " << combined.failed << " failed, " << combined.total << " total\n\n";

                stream << "Failures and warnings\n";
                bool wrote_failure = false;
                for (const auto& message : combined.messages) {
                    if (!extraction_message_is_failure_local(message)) {
                        continue;
                    }
                    stream << "- " << utf8_to_qstring(message) << "\n";
                    wrote_failure = true;
                }
                if (!wrote_failure) {
                    stream << "- none\n";
                }

                stream << "\nOutputs\n";
                if (combined.output_paths.empty()) {
                    stream << "- none\n";
                } else {
                    for (const auto& path : combined.output_paths) {
                        stream << "- " << path_to_qstring(path) << "\n";
                    }
                }

                stream << "\nFull log\n";
                if (combined.messages.empty()) {
                    stream << "- none\n";
                } else {
                    for (const auto& message : combined.messages) {
                        stream << "- " << utf8_to_qstring(message) << "\n";
                    }
                }
                stream.flush();
                report_file.flush();
                combined.diagnostic_path = report_path;
            } else {
                combined.messages.push_back("Could not write extraction report: " + qt_to_utf8_local(report_file.errorString()));
            }
            write_live_log(
                QStringLiteral("Extraction finished: %1 extracted, %2 failed, %3 total")
                    .arg(combined.extracted)
                    .arg(combined.failed)
                    .arg(combined.total)
            );
            if (combined.diagnostic_path.has_value()) {
                write_live_log(QStringLiteral("Extraction report: ") + path_to_qstring(*combined.diagnostic_path));
            }
            return combined;
        }
    ));
    m_work_timer->start(50);
}

void MainWindow::poll_background_work() {
    if (m_load_running) {
        update_loading_indicator();
    }
    if (m_extract_running) {
        update_extraction_indicator();
    }
    if (!m_load_running && !m_extract_running) {
        m_work_timer->stop();
    }
}

void MainWindow::update_loading_indicator() {
    if (m_loading_status_label == nullptr || !m_load_progress) {
        return;
    }

    const auto checked = m_load_progress->candidate_count.load(std::memory_order_relaxed);
    const auto valid = m_load_progress->valid_count.load(std::memory_order_relaxed);
    const auto rejected = m_load_progress->rejected_count.load(std::memory_order_relaxed);
    m_loading_status_label->setText(
        QStringLiteral("%1 checked · %2 valid · %3 rejected").arg(checked).arg(valid).arg(rejected)
    );
}

void MainWindow::update_extraction_indicator() {
    if (m_loading_status_label == nullptr || !m_extract_progress) {
        return;
    }

    const auto total = m_extract_progress->target_count.load(std::memory_order_relaxed);
    const auto processed = m_extract_progress->processed_count.load(std::memory_order_relaxed);
    const auto extracted = m_extract_progress->extracted_count.load(std::memory_order_relaxed);
    const auto failed = m_extract_progress->failed_count.load(std::memory_order_relaxed);
    m_loading_status_label->setText(
        QStringLiteral("%1/%2 targets · %3 extracted · %4 failed")
            .arg(processed)
            .arg(total)
            .arg(extracted)
            .arg(failed)
    );
}

void MainWindow::consume_load_result() {
    auto result = m_load_watcher->future().takeResult();
    m_load_running = false;
    update_loading_indicator();
    if (m_drop_active_load_result) {
        m_drop_active_load_result = false;
        append_log(QStringLiteral("Discarded completed load after clear"));
        if (!m_queued_load_paths.empty()) {
            auto queued = std::move(m_queued_load_paths);
            m_queued_load_paths.clear();
            start_loading_paths(std::move(queued));
            return;
        }
        m_loading_bar->hide();
        if (m_loading_status_label != nullptr) {
            m_loading_status_label->hide();
        }
        m_load_progress.reset();
        statusBar()->showMessage(QStringLiteral("Cleared loaded files"), 3000);
        return;
    }

    size_t added = 0;
    for (const auto& message : result.log_messages) {
        append_log(message);
    }
    m_file_model->reserve(m_file_model->rowCount() + static_cast<int>(result.loaded.size()));
    for (auto& [document, canonical] : result.loaded) {
        if (m_file_model->index_of_path(canonical) >= 0) {
            continue;
        }
        const auto format = utf8_to_qstring(document.format);
        m_file_model->add_document(std::move(document), canonical);
        append_log(QStringLiteral("Loaded: ") + canonical + QStringLiteral(" as ") + format);
        ++added;
    }

    if (!m_queued_load_paths.empty()) {
        auto queued = std::move(m_queued_load_paths);
        m_queued_load_paths.clear();
        start_loading_paths(std::move(queued));
        return;
    }

    m_loading_bar->hide();
    if (m_loading_status_label != nullptr) {
        m_loading_status_label->hide();
    }
    m_load_progress.reset();
    statusBar()->showMessage(
        QStringLiteral("%1 files loaded from %2 valid, %3 rejected, %4 checked")
            .arg(added)
            .arg(result.loaded.size())
            .arg(result.rejected_count)
            .arg(result.candidate_count),
        6000
    );
}

void MainWindow::start_document_materialization(int row) {
    if (m_file_model == nullptr || row < 0) {
        return;
    }
    const auto* document = m_file_model->document_at(row);
    if (document == nullptr || document->summary_loaded) {
        return;
    }

    const auto canonical = m_file_model->canonical_path_at(row);
    if (canonical.isEmpty()) {
        return;
    }
    if (m_materialize_running) {
        if (canonical != m_materialize_canonical_path) {
            m_pending_materialize_canonical_path = canonical;
        }
        return;
    }

    m_materialize_running = true;
    m_materialize_canonical_path = canonical;
    m_pending_materialize_canonical_path.clear();
    const auto request_id = ++m_materialize_request_id;
    auto keys = m_decryption_keys;
    LoadedDocument pending = *document;
    m_materialize_watcher->setFuture(QtConcurrent::run([pending = std::move(pending), canonical, keys = std::move(keys), request_id]() mutable {
        MaterializeResult result;
        result.canonical_path = canonical;
        result.request_id = request_id;
        if (auto loaded = materialize_document_summary(pending, result.rejection_reason, keys)) {
            result.document = std::move(*loaded);
        }
        return result;
    }));
}

void MainWindow::consume_materialize_result() {
    auto result = m_materialize_watcher->future().takeResult();
    m_materialize_running = false;
    m_materialize_canonical_path.clear();

    const auto row = m_file_model == nullptr ? -1 : m_file_model->index_of_path(result.canonical_path);
    if (result.request_id == m_materialize_request_id && row >= 0 && result.document.has_value()) {
        m_file_model->replace_document(row, std::move(*result.document));
        append_log(QStringLiteral("Loaded details: ") + result.canonical_path);

        if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_view->currentIndex().isValid()) {
            const auto current_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (current_source.isValid() && current_source.row() == row) {
                show_document(m_file_model->document_at(row));
            }
        }
    } else if (result.request_id == m_materialize_request_id && row >= 0) {
        const auto message = QStringLiteral("Could not load document details: %1").arg(utf8_to_qstring(result.rejection_reason));
        append_log(message + QStringLiteral(" (") + result.canonical_path + QStringLiteral(")"));
        if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_view->currentIndex().isValid()) {
            const auto current_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (current_source.isValid() && current_source.row() == row) {
                statusBar()->showMessage(message, 5000);
            }
        }
    }

    const auto pending = std::exchange(m_pending_materialize_canonical_path, {});
    if (!pending.isEmpty() && m_file_model != nullptr) {
        const auto pending_row = m_file_model->index_of_path(pending);
        if (pending_row >= 0) {
            start_document_materialization(pending_row);
        }
    }
}

void MainWindow::consume_extract_result() {
    auto report = m_extract_watcher->future().takeResult();
    m_extract_running = false;
    update_extraction_indicator();
    if (!report.messages_logged_live) {
        for (const auto& message : report.messages) {
            append_log(utf8_to_qstring(message));
        }
    }
    if (report.diagnostic_path.has_value()) {
        append_log(QStringLiteral("Extraction report: ") + path_to_qstring(*report.diagnostic_path));
    }

    m_loading_bar->hide();
    if (m_loading_status_label != nullptr) {
        m_loading_status_label->hide();
    }
    m_extract_progress.reset();
    statusBar()->showMessage(
        report.diagnostic_path.has_value()
            ? QStringLiteral("%1 extracted, %2 failed, %3 total · report: %4")
                .arg(report.extracted)
                .arg(report.failed)
                .arg(report.total)
                .arg(path_to_qstring(*report.diagnostic_path))
            : QStringLiteral("%1 extracted, %2 failed, %3 total")
            .arg(report.extracted)
            .arg(report.failed)
            .arg(report.total),
        7000
    );
}

} // namespace cristudio
