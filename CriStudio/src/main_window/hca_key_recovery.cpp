#include "../main_window.hpp"

#include "../path_text.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QStringList>
#include <QToolButton>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace cristudio {
namespace {

[[nodiscard]] QString key_text(uint64_t key) {
    return QStringLiteral("0x%1").arg(
        QString::number(static_cast<qulonglong>(key), 16).toUpper().rightJustified(14, QLatin1Char('0'))
    );
}

[[nodiscard]] QString source_label(const HcaRecoverySource& source) {
    if (!source.name.empty()) {
        return utf8_to_qstring(source.name);
    }
    if (!source.path.empty()) {
        return to_qstring(source.path.filename());
    }
    return QStringLiteral("Selected entry");
}

} // namespace

void MainWindow::start_hca_key_recovery(
    std::vector<HcaRecoverySource> sources,
    QString target_label
) {
    if (sources.empty()) {
        statusBar()->showMessage(QStringLiteral("No files selected for HCA key recovery"), 3000);
        return;
    }
    if (m_hca_key_recovery_running || m_usm_key_recovery_running ||
        m_adx_key_recovery_running || m_aac_key_recovery_running) {
        statusBar()->showMessage(QStringLiteral("Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QStringLiteral("HCA key recovery"), sources.size());
    if (!mode) {
        return;
    }
    if (sources.size() > 1) {
        begin_key_recovery_progress(this, QStringLiteral("HCA key recovery"), sources.size());
    }

    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setEnabled(false);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setEnabled(false);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setEnabled(false);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setEnabled(false);
    }

    statusBar()->showMessage(QStringLiteral("Recovering HCA keys..."));
    m_hca_key_recovery_running = true;
    const auto request_id = ++m_hca_key_recovery_request_id;
    auto keys = m_decryption_keys;
    m_hca_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), keys = std::move(keys), target_label = std::move(target_label), request_id, mode = *mode, window = QPointer<MainWindow>(this)]() mutable {
            HcaKeyRecoveryTaskResult task;
            task.target_label = std::move(target_label);
            task.request_id = request_id;
            task.requested_sources = sources.size();
            std::vector<KeyRecoveryCandidate> displayed;
            KeyRecoveryProgressThrottle progress_throttle;
            const auto publish = [&](QString status) {
                if (task.requested_sources <= 1 || window.isNull()) return;
                const size_t completed = mode == cricodecs::KeyRecoveryMode::SharedBaseKey
                    ? task.requested_sources
                    : task.recovered.size() + task.errors.size();
                if (!progress_throttle.ready(completed, task.requested_sources)) return;
                const auto groups = group_key_recovery_candidates(displayed, MaxInterimKeyRecoveryGroups);
                auto* dispatcher = QCoreApplication::instance();
                if (dispatcher == nullptr) return;
                QMetaObject::invokeMethod(dispatcher, [window, groups, completed, total = task.requested_sources, status = std::move(status)]() mutable {
                    if (!window.isNull()) {
                        update_key_recovery_progress(window, std::move(groups), completed, total, std::move(status));
                    }
                }, Qt::QueuedConnection);
            };
            const auto append_displayed = [&](const HcaRecoveredTarget& target) {
                const float best_score = target.recovered.recovery.candidates.empty()
                    ? 0.0f
                    : target.recovered.recovery.candidates.front().score;
                for (const auto& candidate : target.recovered.recovery.candidates) {
                    if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                    displayed.push_back(KeyRecoveryCandidate{
                        .identity = candidate.key,
                        .key = key_text(candidate.key),
                        .score = candidate.score,
                        .file = target.source,
                    });
                }
            };
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                auto recovered = recover_hca_key(sources, keys, mode);
                if (recovered) {
                    task.recovered.push_back(HcaRecoveredTarget{
                        .recovered = *recovered,
                        .source = task.target_label,
                    });
                    append_displayed(task.recovered.back());
                } else {
                    task.errors.push_back(utf8_to_qstring(recovered.error()));
                }
                publish(QStringLiteral("Shared-base recovery completed."));
            } else {
                struct SourceResult {
                    std::optional<HcaKeyRecoveryResult> recovered;
                    QString label;
                    QString error;
                };
                std::vector<SourceResult> results(sources.size());
                std::atomic_size_t next_source = 0;
                std::atomic_size_t completed_sources = 0;
                std::mutex displayed_mutex;
                const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());
                const auto worker_count = std::min<size_t>({4, sources.size(), hardware_threads});
                std::vector<std::jthread> workers;
                workers.reserve(worker_count);
                for (size_t worker = 0; worker < worker_count; ++worker) {
                    workers.emplace_back([&] {
                        while (true) {
                            const auto index = next_source.fetch_add(1, std::memory_order_relaxed);
                            if (index >= sources.size()) {
                                return;
                            }
                            const auto& source = sources[index];
                            auto& result = results[index];
                            result.label = source_label(source);
                            auto recovered = recover_hca_key(
                                std::span<const HcaRecoverySource>(&source, 1), keys, mode);
                            if (recovered) {
                                result.recovered = std::move(*recovered);
                            } else {
                                result.error = result.label + QStringLiteral(": ") + utf8_to_qstring(recovered.error());
                            }

                            {
                                const std::scoped_lock lock(displayed_mutex);
                                if (result.recovered) {
                                    const auto& candidates = result.recovered->recovery.candidates;
                                    const float best_score = candidates.empty() ? 0.0f : candidates.front().score;
                                    for (const auto& candidate : candidates) {
                                        if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                                        displayed.push_back(KeyRecoveryCandidate{
                                            .identity = candidate.key,
                                            .key = key_text(candidate.key),
                                            .score = candidate.score,
                                            .file = result.label,
                                        });
                                    }
                                }
                                const auto completed = completed_sources.fetch_add(1, std::memory_order_relaxed) + 1;
                                auto groups = group_key_recovery_candidates(displayed, MaxInterimKeyRecoveryGroups);
                                if (!window.isNull()) {
                                    if (auto* dispatcher = QCoreApplication::instance(); dispatcher != nullptr) {
                                        QMetaObject::invokeMethod(dispatcher, [window, groups = std::move(groups), completed, total = sources.size()]() mutable {
                                            if (!window.isNull()) {
                                                update_key_recovery_progress(
                                                    window, std::move(groups), completed, total,
                                                    QStringLiteral("Processed %1 of %2 selected files.")
                                                        .arg(completed)
                                                        .arg(total));
                                            }
                                        }, Qt::QueuedConnection);
                                    }
                                }
                            }
                        }
                    });
                }
                workers.clear();

                task.recovered.reserve(sources.size());
                for (auto& result : results) {
                    if (result.recovered) {
                        task.recovered.push_back(HcaRecoveredTarget{
                            .recovered = std::move(*result.recovered),
                            .source = std::move(result.label),
                        });
                    } else if (!result.error.isEmpty()) {
                        task.errors.push_back(std::move(result.error));
                    }
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_hca_key_recovery_result() {
    auto task = m_hca_key_recovery_watcher->future().takeResult();
    m_hca_key_recovery_running = false;
    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setEnabled(true);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setEnabled(true);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setEnabled(true);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setEnabled(true);
    }
    if (task.request_id != m_hca_key_recovery_request_id) {
        return;
    }

    if (task.recovered.empty()) {
        const auto error = task.errors.empty()
            ? QStringLiteral("No cipher type-56 HCA streams were found.")
            : task.errors.front();
        if (task.requested_sources > 1) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, error, true);
        } else {
            show_key_recovery_error(this, QStringLiteral("HCA key recovery"), error);
        }
        append_log(QStringLiteral("HCA key recovery failed: ") + error);
        statusBar()->showMessage(QStringLiteral("HCA key recovery unavailable"), 5000);
        return;
    }

    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(task.recovered.size() * cricodecs::MaxKeyRecoveryCandidates);
    QString details = QStringLiteral("Target: %1\n\n").arg(task.target_label);
    size_t hca_count = 0;
    for (const auto& target : task.recovered) {
        const float best_score = target.recovered.recovery.candidates.empty()
            ? 0.0f
            : target.recovered.recovery.candidates.front().score;
        for (const auto& recovered : target.recovered.recovery.candidates) {
            if (!show_key_recovery_candidate(recovered.score, best_score)) continue;
            const auto key = key_text(recovered.key);
            const auto score = QString::number(recovered.score, 'f', 6);
            candidates.push_back(KeyRecoveryCandidate{
                .identity = recovered.key,
                .key = key,
                .score = recovered.score,
                .file = target.source,
            });
            details += QStringLiteral("%1\nKey: %2\nScore: %3\nSources: %4\nEvidence: %5\nEquivalents: %6\n\n")
                .arg(target.source, key, score)
                .arg(recovered.source_count)
                .arg(recovered.evidence_count)
                .arg(recovered.equivalent_count);
            append_log(QStringLiteral("HCA key recovery candidate for %1: %2, score %3, %4 equivalent(s)")
                .arg(target.source, key, score)
                .arg(recovered.equivalent_count));
        }
        hca_count += target.recovered.hca_count;
    }
    if (!task.errors.empty()) {
        details += QStringLiteral("Unavailable targets\n");
        for (const auto& error : task.errors) {
            details += QStringLiteral("- %1\n").arg(error);
        }
        details += QLatin1Char('\n');
    }
    details += QStringLiteral(
        "Candidates were not applied globally. Scores are normalized structural agreement, not probability. "
        "Rows are ranked by score, file support, filename, and key.");

    auto groups = group_key_recovery_candidates(candidates);
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        keys.push_back(group.key);
    }
    const auto summary = groups.empty()
        ? QStringLiteral("No positive key candidate was recovered.")
        : task.recovered.size() == 1
        ? QStringLiteral("Recovered key: %1\nScore: %2")
              .arg(groups.front().key)
              .arg(QString::number(groups.front().mean_score, 'f', 6))
        : QStringLiteral("Recovered %1 files in %2 exact-key groups.")
              .arg(task.recovered.size())
              .arg(groups.size());
    if (task.requested_sources > 1) {
        update_key_recovery_progress(
            this,
            groups,
            task.requested_sources,
            task.requested_sources,
            summary,
            true);
    } else {
        show_key_recovery_result(
            this,
            QStringLiteral("HCA key recovery"),
            summary,
            details,
            keys.join(QLatin1Char('\n')),
            groups.size() == 1 ? QStringLiteral("Copy Key") : QStringLiteral("Copy Keys"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        QStringLiteral("HCA key recovery completed for %1 file(s), %2 stream(s)")
            .arg(task.recovered.size())
            .arg(hca_count),
        5000);
}

} // namespace cristudio
