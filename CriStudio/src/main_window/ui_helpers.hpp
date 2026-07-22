#pragma once

#include <QIcon>
#include <QElapsedTimer>
#include <QPalette>
#include <QSlider>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <key_recovery.hpp>

class QLabel;
class QMouseEvent;
class QToolButton;
class QWidget;

namespace cristudio {

class SeekSlider final : public QSlider {
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void set_value_from_position(qreal x);
};

inline constexpr size_t MaxInterimKeyRecoveryGroups = 64;

struct KeyRecoveryGroup {
    uint64_t identity = 0;
    QString key;
    float mean_score = 0.0f;
    size_t count = 0;
    QStringList files;
    size_t omitted_files = 0;
    size_t recommended_count = 0;
};

struct KeyRecoveryCandidate {
    uint64_t identity = 0;
    QString key;
    float score = 0.0f;
    QString file;
    bool recommended = false;
};

[[nodiscard]] std::vector<KeyRecoveryGroup> group_key_recovery_candidates(
    std::span<const KeyRecoveryCandidate> candidates,
    size_t max_groups = 0);
[[nodiscard]] bool show_key_recovery_candidate(float score, float best_score) noexcept;

class KeyRecoveryProgressThrottle {
public:
    [[nodiscard]] bool ready(size_t completed, size_t total);

private:
    QElapsedTimer m_timer;
    size_t m_last_completed = 0;
};

enum class ActionGlyph {
    Extract,
    RawExtract,
    RecoverKey,
    Clear,
    MuxPreview,
};

QString to_qstring(const std::filesystem::path& path);
QString archive_basename(QString text);
void reveal_in_file_manager(const QString& path);
QString strip_mux_prefix(QString text);

QPalette dark_palette();
QPalette light_palette();
QString visual_stylesheet(bool dark);
QString app_title();

QLabel* make_dim_label(QString text, QWidget* parent = nullptr);
QLabel* make_value_label(QString text, QWidget* parent = nullptr);
QIcon make_sidebar_icon(bool panel_on_left);
QIcon make_action_icon(ActionGlyph glyph);
QToolButton* make_panel_button(const QIcon& icon, const QString& tooltip, QWidget* parent);
QToolButton* make_rail_action_button(const QIcon& icon, const QString& tooltip, QWidget* parent);
void fade_widget_in(QWidget* widget, int duration_ms = 140);
[[nodiscard]] std::optional<cricodecs::KeyRecoveryMode> choose_key_recovery_mode(
    QWidget* parent,
    QString title,
    size_t source_count);
void begin_key_recovery_progress(
    QWidget* parent,
    QString title,
    size_t source_count,
    std::function<void()> cancel = {});
void update_key_recovery_progress(
    QWidget* parent,
    std::vector<KeyRecoveryGroup> groups,
    size_t completed,
    size_t total,
    QString status,
    bool finished = false);
void show_key_recovery_result(
    QWidget* parent,
    QString title,
    QString summary,
    QString details,
    QString copy_text = {},
    QString copy_button_text = QStringLiteral("Copy Key"),
    std::vector<KeyRecoveryGroup> groups = {}
);
void show_key_recovery_error(QWidget* parent, QString title, QString error);

} // namespace cristudio
