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
#include <utility>
#include <map>
#include <unordered_set>

namespace cristudio {
namespace {

[[nodiscard]] QString key_text(uint64_t key) {
    return QStringLiteral("0x%1").arg(
        QString::number(static_cast<qulonglong>(key), 16).toUpper().rightJustified(14, QLatin1Char('0'))
    );
}

[[nodiscard]] QString masking_inference(const UsmKeyRecoveryResult& result) {
    if (result.hca_video_supported) {
        return QStringLiteral("Likely masked; embedded HCA and video recovery agree");
    }
    if (result.audio_chunks != 0u && result.audio_score > 0.0f) {
        return QStringLiteral("Likely masked; ADX audio mask evidence was detected");
    }
    if (result.video_chunks != 0u) {
        return QStringLiteral("Unknown; video model agreement is not proof of masking");
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] std::vector<KeyRecoveryGroup> group_results(
    const std::vector<UsmKeyRecoveryResult>& results,
    size_t max_groups = 0) {
    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(results.size());
    std::unordered_set<std::string> ranked_sources;
    for (const auto& result : results) {
        const bool recommended = ranked_sources.insert(result.source).second;
        candidates.push_back(KeyRecoveryCandidate{
            .identity = result.key,
            .key = key_text(result.key),
            .score = result.score,
            .file = utf8_to_qstring(result.source),
            .recommended = recommended,
        });
    }
    return group_key_recovery_candidates(candidates, max_groups);
}

} // namespace

void MainWindow::start_usm_key_recovery(
    std::vector<UsmRecoverySource> sources,
    QString target_label
) {
    if (sources.empty()) {
        statusBar()->showMessage(QStringLiteral("No files selected for USM key recovery"), 3000);
        return;
    }
    if (m_usm_key_recovery_running || m_hca_key_recovery_running ||
        m_adx_key_recovery_running || m_aac_key_recovery_running) {
        statusBar()->showMessage(QStringLiteral("Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QStringLiteral("USM key recovery"), sources.size());
    if (!mode) return;
    if (sources.size() > 1u) {
        begin_key_recovery_progress(this, QStringLiteral("USM key recovery"), sources.size());
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

    statusBar()->showMessage(QStringLiteral("Recovering USM keys..."));
    m_usm_key_recovery_running = true;
    const auto request_id = ++m_usm_key_recovery_request_id;
    auto keys = m_decryption_keys;
    m_usm_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), keys = std::move(keys), target_label = std::move(target_label),
         request_id, mode = *mode, window = QPointer<MainWindow>(this)]() mutable {
            UsmKeyRecoveryTaskResult task;
            task.target_label = std::move(target_label);
            task.request_id = request_id;
            task.requested_sources = sources.size();
            task.report.emplace();
            KeyRecoveryProgressThrottle progress_throttle;
            for (const auto& source : sources) {
                auto partial = recover_usm_keys(std::span<const UsmRecoverySource>(&source, 1), keys);
                if (partial) {
                    task.report->usm_count += partial->usm_count;
                    task.report->recovered.insert(
                        task.report->recovered.end(),
                        std::make_move_iterator(partial->recovered.begin()),
                        std::make_move_iterator(partial->recovered.end()));
                    task.report->errors.insert(
                        task.report->errors.end(),
                        std::make_move_iterator(partial->errors.begin()),
                        std::make_move_iterator(partial->errors.end()));
                } else {
                    task.report->errors.push_back(partial.error());
                }
                if (task.requested_sources > 1u && !window.isNull()) {
                    const size_t completed = std::min(
                        task.requested_sources,
                        task.report->usm_count + task.report->errors.size());
                    if (!progress_throttle.ready(completed, task.requested_sources)) continue;
                    auto groups = group_results(task.report->recovered, MaxInterimKeyRecoveryGroups);
                    auto* dispatcher = QCoreApplication::instance();
                    if (dispatcher == nullptr) continue;
                    QMetaObject::invokeMethod(dispatcher, [window, groups = std::move(groups), completed,
                        total = task.requested_sources]() mutable {
                        if (!window.isNull()) {
                            update_key_recovery_progress(
                                window, std::move(groups), completed, total,
                                QStringLiteral("Processed %1 of %2 selected files.").arg(completed).arg(total));
                        }
                    }, Qt::QueuedConnection);
                }
            }
            if (task.report->usm_count == 0u) {
                task.error = task.report->errors.empty()
                    ? QStringLiteral("No USM containers were found.")
                    : utf8_to_qstring(task.report->errors.front());
                task.report.reset();
                return task;
            }
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                std::map<uint64_t, size_t> support;
                for (const auto& candidate : task.report->recovered) ++support[candidate.key];
                std::erase_if(task.report->recovered, [&](const auto& candidate) {
                    return support[candidate.key] != task.report->usm_count;
                });
                if (task.report->recovered.empty()) {
                    task.error = QStringLiteral("No key candidate was shared by every recovered USM container.");
                    task.report.reset();
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_usm_key_recovery_result() {
    auto task = m_usm_key_recovery_watcher->future().takeResult();
    m_usm_key_recovery_running = false;
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
    if (task.request_id != m_usm_key_recovery_request_id) {
        return;
    }

    if (!task.report) {
        if (task.requested_sources > 1u) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, task.error, true);
        } else {
            show_key_recovery_error(this, QStringLiteral("USM key recovery"), task.error);
        }
        append_log(QStringLiteral("USM key recovery failed: ") + task.error);
        statusBar()->showMessage(QStringLiteral("USM key recovery unavailable"), 5000);
        return;
    }

    std::map<std::string, float> best_by_source;
    for (const auto& result : task.report->recovered) {
        auto& best = best_by_source[result.source];
        best = std::max(best, result.score);
    }
    QString text;
    size_t visible_candidate_count = 0;
    for (const auto& result : task.report->recovered) {
        if (!show_key_recovery_candidate(result.score, best_by_source[result.source])) continue;
        ++visible_candidate_count;
        const auto key = key_text(result.key);
        const auto score = QString::number(result.score, 'f', 6);
        text += QStringLiteral("%1\nKey: %2\nScore: %3\nMasking: %4\n"
                               "Video chunks: %5\nADX audio chunks: %6\nADX audio score: %7\n"
                               "HCA streams: %8\nHCA score: %9\nHCA/video agreement: %10\nSample blocks: %11\n\n")
                    .arg(utf8_to_qstring(result.source))
                    .arg(key)
                    .arg(score)
                    .arg(masking_inference(result))
                    .arg(result.video_chunks)
                    .arg(result.audio_chunks)
                    .arg(QString::number(result.audio_score, 'f', 6))
                    .arg(result.hca_streams)
                    .arg(QString::number(result.hca_score, 'f', 6))
                    .arg(result.hca_video_supported ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(result.sample_blocks);
        append_log(QStringLiteral("USM key recovery completed for %1: %2, score %3")
                       .arg(utf8_to_qstring(result.source), key, score));
    }
    if (!task.report->errors.empty()) {
        text += QStringLiteral("Unavailable targets\n");
        for (const auto& error : task.report->errors) {
            text += QStringLiteral("- %1\n").arg(utf8_to_qstring(error));
        }
        text += QLatin1Char('\n');
    }
    text += QStringLiteral("Candidates were not applied to the global CRI key.\n"
                           "A score ranks model agreement; it is not proof that a USM is masked.");
    auto groups = group_results(task.report->recovered);
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        keys.push_back(group.key);
    }
    const auto candidate_count = visible_candidate_count;
    const auto summary = candidate_count == 0
        ? QStringLiteral("No key candidate was recovered.")
        : (candidate_count == 1
            ? QStringLiteral("Recovered key: %1\nScore: %2")
                  .arg(keys.front())
                  .arg(QString::number(task.report->recovered.front().score, 'f', 6))
            : QStringLiteral("Recovered %1 candidates in %2 exact-key groups.")
                  .arg(candidate_count)
                  .arg(groups.size()));
    if (task.requested_sources > 1u) {
        update_key_recovery_progress(
            this, groups, task.requested_sources, task.requested_sources, summary, true);
    } else {
        show_key_recovery_result(
            this,
            QStringLiteral("USM key recovery"),
            summary,
            QStringLiteral("Target: %1\nUSM containers: %2\n\n%3")
                .arg(task.target_label)
                .arg(task.report->usm_count)
                .arg(text),
            keys.join(QLatin1Char('\n')),
            groups.size() == 1 ? QStringLiteral("Copy Key") : QStringLiteral("Copy Keys"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        task.report->recovered.empty()
            ? QStringLiteral("USM key recovery found no candidate")
            : QStringLiteral("USM key recovery completed"),
        5000);
}

} // namespace cristudio
