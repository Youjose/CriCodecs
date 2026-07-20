#include "../main_window.hpp"

#include "../browser/browser_delegates.hpp"
#include "../editor/hex_preview_widget.hpp"
#include "../editor_workspace.hpp"
#include "key_panel.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"

#include <QAbstractAnimation>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenuBar>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVariantAnimation>
#include <QVideoWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif
#if defined(Q_OS_LINUX) || defined(__linux__)
#include <unistd.h>
#endif
#if defined(Q_OS_MACOS) || defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace cristudio {
namespace {

class FileFilterProxyModel final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void set_search_text(QString text) {
        text = text.trimmed();
        if (m_search_text == text) {
            return;
        }
        beginFilterChange();
        m_search_text = std::move(text);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    void set_type_filters(QStringList types) {
        types.sort(Qt::CaseInsensitive);
        types.removeDuplicates();
        if (m_type_filters == types) {
            return;
        }
        beginFilterChange();
        m_type_filters = std::move(types);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        const auto source_index = sourceModel()->index(source_row, 0, source_parent);
        if (!source_index.isValid()) {
            return false;
        }
        if (!m_type_filters.isEmpty() && !m_type_filters.contains(source_index.data(FileListModel::FilterFormatRole).toString(), Qt::CaseInsensitive)) {
            return false;
        }
        if (m_search_text.isEmpty()) {
            return true;
        }
        return source_index.data(FileListModel::SearchRole)
            .toString()
            .contains(m_search_text, Qt::CaseInsensitive);
    }

private:
    QString m_search_text;
    QStringList m_type_filters;
};

class FileTypeFilterCombo final : public QComboBox {
public:
    using QComboBox::QComboBox;

    void set_selection_changed(std::function<void()> callback) {
        m_selection_changed = std::move(callback);
    }

protected:
    void showPopup() override {
        QComboBox::showPopup();
        if (view() != nullptr && view()->viewport() != nullptr) {
            view()->viewport()->installEventFilter(this);
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (view() != nullptr
            && watched == view()->viewport()
            && event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const auto index = view()->indexAt(mouse->position().toPoint());
            if (!index.isValid()) {
                return QComboBox::eventFilter(watched, event);
            }

            if (index.row() == 0) {
                for (int item = 1; item < count(); ++item) {
                    setItemData(item, Qt::Unchecked, Qt::CheckStateRole);
                }
            } else {
                const auto state = itemData(index.row(), Qt::CheckStateRole).value<Qt::CheckState>();
                setItemData(index.row(), state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            }

            if (m_selection_changed) {
                m_selection_changed();
            }
            return true;
        }
        return QComboBox::eventFilter(watched, event);
    }

private:
    std::function<void()> m_selection_changed;
};

QStringList selected_file_type_filters(const QComboBox* combo) {
    QStringList selected;
    if (combo == nullptr) {
        return selected;
    }
    for (int index = 1; index < combo->count(); ++index) {
        if (combo->itemData(index, Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked) {
            selected.push_back(combo->itemData(index).toString());
        }
    }
    return selected;
}

void update_file_type_filter_label(QComboBox* combo) {
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    const auto selected = selected_file_type_filters(combo);
    const auto label = selected.isEmpty()
        ? QStringLiteral("All types")
        : (selected.size() == 1 ? selected.front() : QStringLiteral("%1 types").arg(selected.size()));
    combo->setItemText(0, label);
    combo->setItemData(0, selected.isEmpty() ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    combo->setCurrentIndex(0);
}

void refresh_file_type_filter(QComboBox* combo, const FileListModel* model) {
    if (combo == nullptr || model == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    const auto previous = selected_file_type_filters(combo);
    QStringList formats;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto format = model->index(row, 0).data(FileListModel::FilterFormatRole).toString();
        if (!format.isEmpty() && !formats.contains(format, Qt::CaseInsensitive)) {
            formats.push_back(format);
        }
    }
    formats.sort(Qt::CaseInsensitive);

    combo->clear();
    combo->addItem(QStringLiteral("All types"), QString{});
    combo->setItemData(0, Qt::Checked, Qt::CheckStateRole);
    for (const auto& format : formats) {
        combo->addItem(format, format);
        combo->setItemData(combo->count() - 1, previous.contains(format, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    }
    update_file_type_filter_label(combo);
}

void sync_segment_buttons(QWidget* selector, int active) {
    if (selector == nullptr) {
        return;
    }
    const auto buttons = selector->findChildren<QToolButton*>(QStringLiteral("EntryViewModeSegment"));
    for (auto* button : buttons) {
        const QSignalBlocker blocker(button);
        button->setChecked(button->property("modeValue").toInt() == active);
    }
}

class ColumnSplitterHandle final : public QSplitterHandle {
public:
    explicit ColumnSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {
        setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QSplitterHandle::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const auto line = palette().mid().color();
        auto grip = line;
        grip.setAlpha(150);
        painter.setBrush(line);
        if (orientation() == Qt::Horizontal) {
            const auto x = width() / 2;
            painter.drawRect(QRect(x, 0, 1, height()));
            painter.setBrush(grip);
            const auto center = rect().center();
            for (int i = -1; i <= 1; ++i) {
                painter.drawRoundedRect(QRectF(center.x() - 1.5, center.y() + (i * 7) - 1.5, 3.0, 3.0), 1.5, 1.5);
            }
        } else {
            const auto y = height() / 2;
            painter.drawRect(QRect(0, y, width(), 1));
            painter.setBrush(grip);
            const auto center = rect().center();
            for (int i = -1; i <= 1; ++i) {
                painter.drawRoundedRect(QRectF(center.x() + (i * 7) - 1.5, center.y() - 1.5, 3.0, 3.0), 1.5, 1.5);
            }
        }
    }
};

class ColumnSplitter final : public QSplitter {
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle* createHandle() override {
        return new ColumnSplitterHandle(orientation(), this);
    }
};

class AutoHideRail final : public QWidget {
public:
    explicit AutoHideRail(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_Hover);
        setFixedWidth(m_hidden_width);
        m_hide_timer.setSingleShot(true);
        m_hide_timer.setInterval(220);
        QObject::connect(&m_hide_timer, &QTimer::timeout, this, [this] {
            if (underMouse() || (m_hover_guard != nullptr && m_hover_guard->underMouse())) {
                m_hide_timer.start();
                return;
            }
            conceal();
        });
        m_animation.setDuration(140);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            setFixedWidth(value.toInt());
            updateGeometry();
        });
    }

public:
    [[nodiscard]] bool expanded() const noexcept { return m_expanded; }

    void reveal() {
        set_expanded(true);
    }

    void conceal() {
        if (!m_auto_hide_enabled) {
            return;
        }
        set_expanded(false);
    }

    void set_auto_hide_enabled(bool enabled) {
        m_auto_hide_enabled = enabled;
        if (!enabled) {
            m_hide_timer.stop();
            reveal();
        } else if (!underMouse()) {
            conceal();
        }
    }

    void set_expanded_changed(std::function<void(bool)> callback) {
        m_expanded_changed = std::move(callback);
    }

    void set_hover_guard(QWidget* widget) {
        m_hover_guard = widget;
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        m_hide_timer.start();
    }

private:
    void set_expanded(bool expanded) {
        const auto target = expanded ? m_shown_width : m_hidden_width;
        if (m_expanded == expanded && width() == target && m_animation.state() != QAbstractAnimation::Running) {
            return;
        }
        m_expanded = expanded;
        if (m_expanded_changed) {
            m_expanded_changed(expanded);
        }
        m_animation.stop();
        m_animation.setStartValue(width());
        m_animation.setEndValue(target);
        m_animation.start();
    }

    static constexpr int m_hidden_width = 0;
    static constexpr int m_shown_width = 42;
    bool m_auto_hide_enabled = true;
    bool m_expanded = false;
    QTimer m_hide_timer;
    QVariantAnimation m_animation;
    std::function<void(bool)> m_expanded_changed;
    QWidget* m_hover_guard = nullptr;
};

class AutoHideTabBar final : public QTabBar {
public:
    explicit AutoHideTabBar(QWidget* parent = nullptr)
        : QTabBar(parent) {
        setObjectName(QStringLiteral("WorkspaceShelfTabs"));
        setAttribute(Qt::WA_Hover);
        setExpanding(false);
        setUsesScrollButtons(false);
        setMouseTracking(true);
        setFixedHeight(m_hidden_height);
        m_hide_timer.setSingleShot(true);
        m_hide_timer.setInterval(260);
        QObject::connect(&m_hide_timer, &QTimer::timeout, this, [this] {
            conceal();
        });
        m_animation.setDuration(150);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            setFixedHeight(value.toInt());
            updateGeometry();
        });
    }

    [[nodiscard]] QSize sizeHint() const override {
        auto size = QTabBar::sizeHint();
        size.setHeight(height());
        return size;
    }

    [[nodiscard]] QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        size.setHeight(height());
        return size;
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QTabBar::enterEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void leaveEvent(QEvent* event) override {
        QTabBar::leaveEvent(event);
        m_hide_timer.start();
    }

    void focusInEvent(QFocusEvent* event) override {
        QTabBar::focusInEvent(event);
        m_hide_timer.stop();
        reveal();
    }

    void focusOutEvent(QFocusEvent* event) override {
        QTabBar::focusOutEvent(event);
        conceal();
    }

public:
    [[nodiscard]] bool expanded() const noexcept { return m_expanded; }

    void reveal() {
        set_expanded(true);
    }

    void conceal() {
        if (!m_auto_hide_enabled) {
            return;
        }
        set_expanded(false);
    }

    void set_auto_hide_enabled(bool enabled) {
        m_auto_hide_enabled = enabled;
        if (!enabled) {
            m_hide_timer.stop();
            reveal();
        }
    }

    void set_expanded_changed(std::function<void(bool)> callback) {
        m_expanded_changed = std::move(callback);
    }

private:
    void set_expanded(bool expanded) {
        const auto target = expanded ? m_shown_height : m_hidden_height;
        if (m_expanded == expanded && height() == target && m_animation.state() != QAbstractAnimation::Running) {
            return;
        }
        m_expanded = expanded;
        if (m_expanded_changed) {
            m_expanded_changed(expanded);
        }
        m_animation.stop();
        m_animation.setStartValue(height());
        m_animation.setEndValue(target);
        m_animation.start();
    }

    static constexpr int m_hidden_height = 0;
    static constexpr int m_shown_height = 30;
    bool m_auto_hide_enabled = true;
    bool m_expanded = false;
    QTimer m_hide_timer;
    QVariantAnimation m_animation;
    std::function<void(bool)> m_expanded_changed;
};

class ContentHostWidget final : public QWidget {
public:
    using QWidget::QWidget;

    void set_edge_hover_bands(QWidget* left, QWidget* right) {
        m_left_hover_band = left;
        m_right_hover_band = right;
        position_edge_hover_bands();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        position_edge_hover_bands();
    }

private:
    void position_edge_hover_bands() {
        if (m_left_hover_band != nullptr) {
            m_left_hover_band->setGeometry(0, 0, 30, height());
            m_left_hover_band->raise();
        }
        if (m_right_hover_band != nullptr) {
            m_right_hover_band->setGeometry((std::max)(0, width() - 30), 0, 30, height());
            m_right_hover_band->raise();
        }
    }

    QWidget* m_left_hover_band = nullptr;
    QWidget* m_right_hover_band = nullptr;
};

class EdgeHoverBand final : public QWidget {
public:
    explicit EdgeHoverBand(std::function<void()> callback, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_callback(std::move(callback)) {
        setObjectName(QStringLiteral("EdgeHoverBand"));
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, true);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        if (m_callback) {
            m_callback();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        QWidget::mouseMoveEvent(event);
        if (m_callback) {
            m_callback();
        }
    }

private:
    std::function<void()> m_callback;
};

class WorkspaceTabWidget final : public QTabWidget {
public:
    explicit WorkspaceTabWidget(QWidget* parent = nullptr)
        : QTabWidget(parent) {
        if (auto* app = QApplication::instance(); app != nullptr) {
            app->installEventFilter(this);
        }
        m_tabs = new AutoHideTabBar(this);
        setTabBar(m_tabs);
        m_hover_band = new QWidget(this);
        m_hover_band->setObjectName(QStringLiteral("WorkspaceShelfHoverBand"));
        m_hover_band->setMouseTracking(true);
        m_hover_band->installEventFilter(this);
        m_tabs->set_expanded_changed([this](bool expanded) {
            if (m_hover_band == nullptr) {
                return;
            }
            m_hover_band->setVisible(!m_ribbon_locked && currentIndex() == 0 && !expanded);
            if (expanded && currentIndex() == 0) {
                start_top_hide_watch();
            } else if (!expanded && m_top_hide_timer != nullptr) {
                m_top_hide_timer->stop();
            }
            if (!expanded) {
                m_hover_band->raise();
            }
        });
        m_top_hide_timer = new QTimer(this);
        m_top_hide_timer->setInterval(180);
        QObject::connect(m_top_hide_timer, &QTimer::timeout, this, [this] {
            if (m_tabs == nullptr || currentIndex() != 0 || !m_tabs->expanded()) {
                m_top_hide_timer->stop();
                return;
            }
            const auto top_band = QRect(mapToGlobal(QPoint(edge_hover_width, 0)), QSize((std::max)(0, width() - edge_hover_width * 2), 30));
            if (!top_band.contains(QCursor::pos())) {
                m_tabs->conceal();
                m_top_hide_timer->stop();
            }
        });
        QObject::connect(this, &QTabWidget::currentChanged, this, [this] {
            update_auto_hide_state();
        });
    }

    ~WorkspaceTabWidget() override {
        if (auto* app = QApplication::instance(); app != nullptr) {
            app->removeEventFilter(this);
        }
    }

    void set_ribbon_locked(bool locked) {
        m_ribbon_locked = locked;
        update_auto_hide_state();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabWidget::resizeEvent(event);
        if (m_hover_band != nullptr) {
            m_hover_band->setGeometry(edge_hover_width, 0, (std::max)(0, width() - edge_hover_width * 2), 30);
            m_hover_band->setVisible(!m_ribbon_locked && currentIndex() == 0 && m_tabs != nullptr && !m_tabs->expanded());
            m_hover_band->raise();
        }
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (
            currentIndex() == 0 &&
            !m_ribbon_locked &&
            m_tabs != nullptr &&
            !m_tabs->expanded() &&
            (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)
        ) {
            const auto local_pos = mapFromGlobal(QCursor::pos());
            if (QRect(edge_hover_width, 0, (std::max)(0, width() - edge_hover_width * 2), 30).contains(local_pos)) {
                m_tabs->reveal();
                start_top_hide_watch();
            }
        }
        if (
            object == m_hover_band &&
            !m_ribbon_locked &&
            (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)
        ) {
            if (m_tabs != nullptr) {
                m_tabs->reveal();
                start_top_hide_watch();
            }
        }
        return QTabWidget::eventFilter(object, event);
    }

private:
    void start_top_hide_watch() {
        if (m_top_hide_timer != nullptr && currentIndex() == 0) {
            m_top_hide_timer->start();
        }
    }

    void update_auto_hide_state() {
        if (m_tabs == nullptr || m_hover_band == nullptr) {
            return;
        }
        const auto browse_active = currentIndex() == 0;
        m_tabs->set_auto_hide_enabled(!m_ribbon_locked && browse_active);
        if (!m_ribbon_locked && browse_active) {
            if (top_trigger_contains_cursor()) {
                m_tabs->reveal();
                start_top_hide_watch();
            } else {
                m_tabs->conceal();
            }
        }
        m_hover_band->setVisible(!m_ribbon_locked && browse_active && !m_tabs->expanded());
        if (m_hover_band->isVisible()) {
            m_hover_band->raise();
        }
    }

    [[nodiscard]] bool top_trigger_contains_cursor() const {
        return QRect(
            mapToGlobal(QPoint(edge_hover_width, 0)),
            QSize((std::max)(0, width() - edge_hover_width * 2), 30)
        ).contains(QCursor::pos());
    }

    AutoHideTabBar* m_tabs = nullptr;
    QWidget* m_hover_band = nullptr;
    QTimer* m_top_hide_timer = nullptr;
    bool m_ribbon_locked = false;
    static constexpr int edge_hover_width = 30;
};

bool is_mux_document(const LoadedDocument& document) {
    const auto format = utf8_to_qstring(document.format).toLower();
    return format.contains(QStringLiteral("usm")) ||
           format.contains(QStringLiteral("sfd")) ||
           format.contains(QStringLiteral("sofdec"));
}

std::optional<uint64_t> resident_memory_bytes() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters)
        ) == 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(counters.WorkingSetSize);
#elif defined(Q_OS_LINUX) || defined(__linux__)
    QFile file(QStringLiteral("/proc/self/status"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const auto line = file.readLine();
            if (!line.startsWith("VmRSS:")) {
                continue;
            }
            const auto text = QString::fromLatin1(line).simplified();
            const auto parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 2) {
                break;
            }
            bool ok = false;
            const auto kib = parts[1].toULongLong(&ok);
            if (ok) {
                return static_cast<uint64_t>(kib) * 1024ull;
            }
            break;
        }
    }

    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto line = QString::fromLatin1(statm.readLine()).simplified();
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool ok = false;
            const auto pages = parts[1].toULongLong(&ok);
            const auto page_size = sysconf(_SC_PAGESIZE);
            if (ok && page_size > 0) {
                return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
            }
        }
    }

    return std::nullopt;
#elif defined(Q_OS_MACOS) || defined(__APPLE__)
    mach_task_basic_info info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result = task_info(
        mach_task_self(),
        MACH_TASK_BASIC_INFO,
        reinterpret_cast<task_info_t>(&info),
        &count
    );
    if (result != KERN_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(info.resident_size);
#else
    return std::nullopt;
#endif
}

QString format_memory_size(uint64_t bytes) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = mib * 1024.0;
    const auto value = static_cast<double>(bytes);
    if (value >= gib) {
        return QStringLiteral("%1 GB").arg(value / gib, 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(value / mib, 0, 'f', value >= 100.0 * mib ? 0 : 1);
}



} // namespace

void MainWindow::build_ui() {
    m_file_model = new FileListModel(this);
    auto* file_proxy = new FileFilterProxyModel(this);
    m_file_proxy = file_proxy;
    m_file_proxy->setSourceModel(m_file_model);
    m_file_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_file_proxy->sort(0);

    m_entry_model = new EntryTableModel(this);
    m_entry_proxy = new QSortFilterProxyModel(this);
    m_entry_proxy->setSourceModel(m_entry_model);
    m_entry_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_entry_proxy->setFilterKeyColumn(0);
    m_entry_proxy->setFilterRole(EntryTableModel::FullPathRole);
    m_entry_proxy->setSortRole(EntryTableModel::SortRole);
    m_entry_proxy->setRecursiveFilteringEnabled(true);

    m_nested_entry_model = new EntryTableModel(this);

    m_file_filter = new QLineEdit(this);
    m_file_filter->setObjectName(QStringLiteral("SearchField"));
    m_file_filter->setPlaceholderText(QStringLiteral("Search files"));
    m_file_sort = new QComboBox(this);
    m_file_sort->setObjectName(QStringLiteral("FileSortCombo"));
    m_file_sort->setToolTip(QStringLiteral("Sort loaded assets"));
    m_file_sort->addItem(QStringLiteral("Name A-Z"), QVariant::fromValue<int>(0));
    m_file_sort->addItem(QStringLiteral("Name Z-A"), QVariant::fromValue<int>(1));
    m_file_sort->addItem(QStringLiteral("Smallest"), QVariant::fromValue<int>(2));
    m_file_sort->addItem(QStringLiteral("Largest"), QVariant::fromValue<int>(3));
    m_file_type_filter = new FileTypeFilterCombo(this);
    m_file_type_filter->setObjectName(QStringLiteral("FileTypeFilterCombo"));
    m_file_type_filter->setToolTip(QStringLiteral("Filter loaded files by detected type"));
    m_file_type_filter->view()->setMouseTracking(true);
    refresh_file_type_filter(m_file_type_filter, m_file_model);
    m_file_view = new QListView(this);
    m_file_view->setObjectName(QStringLiteral("LoadedFileView"));
    m_file_view->setModel(m_file_proxy);
    m_file_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_file_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_file_view->setUniformItemSizes(true);
    m_file_view->setAlternatingRowColors(false);
    m_file_view->setMouseTracking(true);
    m_file_view->setSpacing(2);
    m_file_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_file_view->setItemDelegate(new LoadedFileDelegate(m_file_filter, m_file_view));
    m_file_list_status = new QLabel(QStringLiteral("0 loaded"), this);
    m_file_list_status->setObjectName(QStringLiteral("FileListStatus"));

    m_left_panel_button = make_panel_button(make_sidebar_icon(true), QStringLiteral("Toggle loaded files panel"), this);
    m_left_panel_button->setChecked(true);
    connect(m_left_panel_button, &QToolButton::clicked, this, [this](bool checked) {
        if (m_toggle_left_action != nullptr) {
            m_toggle_left_action->setChecked(checked);
        }
        toggle_left_panel();
    });
    m_clear_files_button = make_rail_action_button(
        make_action_icon(ActionGlyph::Clear),
        QStringLiteral("Unload all files"),
        this
    );
    connect(m_clear_files_button, &QToolButton::clicked, this, &MainWindow::clear_loaded_files);

    m_left_panel = new QWidget(this);
    m_left_panel->setObjectName(QStringLiteral("LoadedPanel"));
    auto* left_layout = new QVBoxLayout(m_left_panel);
    left_layout->setContentsMargins(8, 8, 8, 8);
    left_layout->setSpacing(6);
    auto* file_sort_row = new QWidget(m_left_panel);
    auto* file_sort_layout = new QHBoxLayout(file_sort_row);
    file_sort_layout->setContentsMargins(0, 0, 0, 0);
    file_sort_layout->setSpacing(6);
    file_sort_layout->addWidget(m_file_sort, 1);
    file_sort_layout->addWidget(m_file_type_filter, 1);
    left_layout->addWidget(m_file_filter);
    left_layout->addWidget(file_sort_row);
    left_layout->addWidget(m_file_view, 1);
    left_layout->addWidget(m_file_list_status);
    m_left_panel->setMinimumWidth(260);

    m_doc_title = new QLabel(QStringLiteral("No file selected"), this);
    m_doc_title->setObjectName(QStringLiteral("DocumentTitle"));
    m_doc_subtitle = new QLabel(QStringLiteral("Open or drop CRI files to inspect them."), this);
    m_doc_subtitle->setObjectName(QStringLiteral("DocumentSubtitle"));

    auto* info_content = new QWidget(this);
    m_info_grid = new QGridLayout(info_content);
    m_info_grid->setContentsMargins(0, 0, 0, 0);
    m_info_grid->setHorizontalSpacing(18);
    m_info_grid->setVerticalSpacing(2);
    info_content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_doc_key_panel = make_key_panel(m_doc_key_label, m_doc_key_input, m_doc_key_base_input, m_doc_key_apply, this);
    m_doc_mux_preview_button = new QToolButton(this);
    m_doc_mux_preview_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_mux_preview_button->setText(QStringLiteral("Mux preview"));
    m_doc_mux_preview_button->setIcon(make_action_icon(ActionGlyph::MuxPreview));
    m_doc_mux_preview_button->setIconSize(QSize(18, 18));
    m_doc_mux_preview_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_mux_preview_button->setToolTip(QStringLiteral("Return to the composed USM/SFD preview"));
    m_doc_mux_preview_button->setAccessibleName(QStringLiteral("Show mux preview"));
    m_doc_mux_preview_button->hide();
    m_doc_extract_button = new QToolButton(this);
    m_doc_extract_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_extract_button->setText(QStringLiteral("Extract All"));
    m_doc_extract_button->setIcon(make_action_icon(ActionGlyph::Extract));
    m_doc_extract_button->setIconSize(QSize(18, 18));
    m_doc_extract_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_extract_button->setToolTip(QStringLiteral("Extract the selected loaded file"));
    m_doc_extract_button->setAccessibleName(QStringLiteral("Extract selected loaded file"));
    m_doc_extract_button->hide();
    m_doc_extract_raw_button = new QToolButton(this);
    m_doc_extract_raw_button->setObjectName(QStringLiteral("ActionButton"));
    m_doc_extract_raw_button->setText(QStringLiteral("All Raw"));
    m_doc_extract_raw_button->setIcon(make_action_icon(ActionGlyph::RawExtract));
    m_doc_extract_raw_button->setIconSize(QSize(18, 18));
    m_doc_extract_raw_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_doc_extract_raw_button->setToolTip(QStringLiteral("Extract the selected loaded file without decode or mux conversion"));
    m_doc_extract_raw_button->setAccessibleName(QStringLiteral("Raw extract selected loaded file"));
    m_doc_extract_raw_button->hide();

    m_entry_filter = new QLineEdit(this);
    m_entry_filter->setObjectName(QStringLiteral("SearchField"));
    m_entry_filter->setPlaceholderText(QStringLiteral("Search entries"));
    m_entry_view_mode = new QComboBox(this);
    m_entry_view_mode->setObjectName(QStringLiteral("EntryViewModeCombo"));
    m_entry_view_mode->setToolTip(QStringLiteral("Choose archive entry view mode"));
    m_entry_view_mode->addItem(QStringLiteral("Tree"), QVariant::fromValue<int>(0));
    m_entry_view_mode->addItem(QStringLiteral("List"), QVariant::fromValue<int>(1));
    m_entry_view_mode->setMinimumWidth(116);
    m_entry_view_mode->hide();
    auto* entry_view_selector = new QWidget(this);
    entry_view_selector->setObjectName(QStringLiteral("EntryViewModeSelector"));
    auto* entry_view_selector_layout = new QHBoxLayout(entry_view_selector);
    entry_view_selector_layout->setContentsMargins(0, 0, 0, 0);
    entry_view_selector_layout->setSpacing(0);
    entry_view_selector_layout->addWidget(m_entry_view_mode, 0);
    for (const auto& item : {std::pair{QStringLiteral("Tree"), 0}, std::pair{QStringLiteral("List"), 1}}) {
        auto* button = new QToolButton(entry_view_selector);
        button->setObjectName(QStringLiteral("EntryViewModeSegment"));
        button->setText(item.first);
        button->setProperty("modeValue", item.second);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(QStringLiteral("Show entries as %1").arg(item.first.toLower()));
        entry_view_selector_layout->addWidget(button, 0);
        connect(button, &QToolButton::clicked, m_entry_view_mode, [this, value = item.second] {
            m_entry_view_mode->setCurrentIndex(value);
        });
    }
    sync_segment_buttons(entry_view_selector, 0);
    m_entry_view = new QTreeView(this);
    m_entry_view->setObjectName(QStringLiteral("EntryTree"));
    m_entry_view->setModel(m_entry_proxy);
    m_entry_view->setAlternatingRowColors(false);
    m_entry_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entry_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_entry_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entry_view->setUniformRowHeights(true);
    m_entry_view->setAnimated(false);
    m_entry_view->setMouseTracking(true);
    m_entry_view->setRootIsDecorated(true);
    m_entry_view->setItemDelegate(new EntryTreeDelegate(m_entry_filter, m_file_filter, m_entry_view));
    m_entry_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_entry_view->setSortingEnabled(true);
    m_entry_view->installEventFilter(this);
    m_entry_view->sortByColumn(0, Qt::AscendingOrder);
    m_entry_view->header()->setSectionsClickable(true);
    m_entry_view->header()->setSortIndicatorShown(true);
    m_entry_view->header()->setStretchLastSection(false);
    m_entry_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_entry_view->header()->setMinimumSectionSize(64);
    m_entry_view->header()->resizeSection(0, 340);
    m_entry_view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_entry_view->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_entry_view->setColumnWidth(1, 120);
    m_entry_view->setColumnWidth(2, 120);
    m_entry_view->setColumnWidth(3, 100);
    m_entry_view->setColumnWidth(4, 220);
    m_entry_filter->hide();
    m_entry_view->hide();
    m_entry_selection_status = new QLabel(QStringLiteral("0 selected"), this);
    m_entry_selection_status->setObjectName(QStringLiteral("EntrySelectionStatus"));
    m_entry_selection_status->hide();

    auto* right = new QWidget(this);
    right->setObjectName(QStringLiteral("DocumentPanel"));
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(8, 8, 8, 8);
    right_layout->setSpacing(6);
    right_layout->addWidget(m_doc_title);
    right_layout->addWidget(m_doc_subtitle);
    right_layout->addWidget(info_content);
    auto* doc_actions = new QHBoxLayout();
    doc_actions->setContentsMargins(0, 0, 0, 0);
    doc_actions->setSpacing(8);
    doc_actions->addWidget(m_doc_extract_button, 0);
    doc_actions->addWidget(m_doc_extract_raw_button, 0);
    doc_actions->addStretch(1);
    doc_actions->addWidget(m_doc_mux_preview_button, 0);
    right_layout->addLayout(doc_actions);
    right_layout->addWidget(m_doc_key_panel);
    m_entry_filter_row = new QWidget(this);
    auto* entry_filter_layout = new QHBoxLayout(m_entry_filter_row);
    entry_filter_layout->setContentsMargins(0, 0, 0, 0);
    entry_filter_layout->setSpacing(8);
    entry_filter_layout->addWidget(m_entry_filter, 1);
    entry_filter_layout->addWidget(entry_view_selector, 0);
    right_layout->addWidget(m_entry_filter_row);
    m_entry_filter_row->hide();
    m_entry_path_row = new QWidget(this);
    auto* entry_path_layout = new QHBoxLayout(m_entry_path_row);
    entry_path_layout->setContentsMargins(0, 0, 0, 0);
    entry_path_layout->setSpacing(6);
    m_entry_up_button = new QToolButton(m_entry_path_row);
    m_entry_up_button->setObjectName(QStringLiteral("SmallToolButton"));
    m_entry_up_button->setText(QStringLiteral("Up"));
    m_entry_up_button->setToolTip(QStringLiteral("Go to the parent archive folder"));
    m_entry_up_button->setAccessibleName(QStringLiteral("Go to parent archive folder"));
    m_entry_path_label = new QLabel(m_entry_path_row);
    m_entry_path_label->setObjectName(QStringLiteral("EntryPathLabel"));
    m_entry_path_label->setTextFormat(Qt::RichText);
    m_entry_path_label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_entry_up_button->hide();
    entry_path_layout->addWidget(m_entry_up_button, 0);
    entry_path_layout->addWidget(m_entry_path_label, 1);
    right_layout->addWidget(m_entry_path_row);
    m_entry_path_row->hide();
    right_layout->addWidget(m_entry_view, 1);
    right_layout->addWidget(m_entry_selection_status);
    m_main_bottom_spacer = new QWidget(right);
    m_main_bottom_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    right_layout->addWidget(m_main_bottom_spacer, 1);

    m_nested_panel = new QWidget(this);
    m_nested_panel->setObjectName(QStringLiteral("PreviewPanel"));
    m_nested_panel->setMinimumWidth(0);
    auto* nested_layout = new QVBoxLayout(m_nested_panel);
    nested_layout->setContentsMargins(8, 8, 8, 8);
    nested_layout->setSpacing(8);
    m_preview_panel_button = make_panel_button(make_sidebar_icon(false), QStringLiteral("Toggle entry preview panel"), m_nested_panel);
    m_preview_panel_button->setChecked(false);
    connect(m_preview_panel_button, &QToolButton::clicked, this, [this](bool checked) {
        if (m_toggle_preview_action != nullptr) {
            m_toggle_preview_action->setChecked(checked);
        }
        toggle_preview_panel();
    });
    m_nested_title = new QLabel(QStringLiteral("Entry preview"), m_nested_panel);
    m_nested_title->setObjectName(QStringLiteral("PreviewTitle"));
    m_nested_subtitle = new QLabel(QStringLiteral("Select a supported embedded file."), m_nested_panel);
    m_nested_subtitle->setObjectName(QStringLiteral("PreviewSubtitle"));
    m_preview_extract_button = new QToolButton(m_nested_panel);
    m_preview_extract_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_extract_button->setText(QStringLiteral("Extract Entry"));
    m_preview_extract_button->setIcon(make_action_icon(ActionGlyph::Extract));
    m_preview_extract_button->setIconSize(QSize(18, 18));
    m_preview_extract_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_extract_button->setToolTip(QStringLiteral("Extract this previewed archive entry"));
    m_preview_extract_button->setAccessibleName(QStringLiteral("Extract previewed archive entry"));
    m_preview_extract_button->hide();
    m_preview_extract_raw_button = new QToolButton(m_nested_panel);
    m_preview_extract_raw_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_extract_raw_button->setText(QStringLiteral("Entry Raw"));
    m_preview_extract_raw_button->setIcon(make_action_icon(ActionGlyph::RawExtract));
    m_preview_extract_raw_button->setIconSize(QSize(18, 18));
    m_preview_extract_raw_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_extract_raw_button->setToolTip(QStringLiteral("Extract this previewed archive entry without decode or mux conversion"));
    m_preview_extract_raw_button->setAccessibleName(QStringLiteral("Raw extract previewed archive entry"));
    m_preview_extract_raw_button->hide();
    m_preview_recover_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_key_button->setText(QStringLiteral("Recover HCA Key"));
    m_preview_recover_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_key_button->setToolTip(QStringLiteral("Recover an HCA type-56 key from this previewed file"));
    m_preview_recover_key_button->setAccessibleName(QStringLiteral("Recover HCA key from previewed file"));
    m_preview_recover_key_button->hide();
    m_preview_recover_usm_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_usm_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_usm_key_button->setText(QStringLiteral("Recover USM Key"));
    m_preview_recover_usm_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_usm_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_usm_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_usm_key_button->setToolTip(QStringLiteral("Recover a USM mask key from audio and video evidence"));
    m_preview_recover_usm_key_button->setAccessibleName(QStringLiteral("Recover USM key from previewed file"));
    m_preview_recover_usm_key_button->hide();
    m_preview_recover_adx_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_adx_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_adx_key_button->setText(QStringLiteral("Recover ADX Key"));
    m_preview_recover_adx_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_adx_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_adx_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_adx_key_button->setAccessibleName(QStringLiteral("Recover ADX or AHX key from previewed file"));
    m_preview_recover_adx_key_button->hide();
    m_preview_recover_aac_key_button = new QToolButton(m_nested_panel);
    m_preview_recover_aac_key_button->setObjectName(QStringLiteral("ActionButton"));
    m_preview_recover_aac_key_button->setText(QStringLiteral("Recover AAC Key"));
    m_preview_recover_aac_key_button->setIcon(make_action_icon(ActionGlyph::RecoverKey));
    m_preview_recover_aac_key_button->setIconSize(QSize(18, 18));
    m_preview_recover_aac_key_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview_recover_aac_key_button->setToolTip(QStringLiteral("Recover the effective AAC key from this ACB/AWB M4A source"));
    m_preview_recover_aac_key_button->setAccessibleName(QStringLiteral("Recover AAC key from previewed ACB or AWB source"));
    m_preview_recover_aac_key_button->hide();
    auto* nested_content = new QWidget(m_nested_panel);
    m_nested_info_panel = nested_content;
    m_nested_info_grid = new QGridLayout(nested_content);
    m_nested_info_grid->setContentsMargins(0, 0, 0, 0);
    m_nested_info_grid->setHorizontalSpacing(16);
    m_nested_info_grid->setVerticalSpacing(5);
    m_nested_info_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_preview_key_panel = make_key_panel(m_preview_key_label, m_preview_key_input, m_preview_key_base_input, m_preview_key_apply, m_nested_panel);
    m_preview_tabs = new QTabWidget(m_nested_panel);
    m_preview_tabs->setObjectName(QStringLiteral("PreviewTabs"));
    m_preview_tabs->setDocumentMode(false);
    m_preview_tabs->hide();
    m_preview_tab = new QWidget(m_preview_tabs);
    auto* preview_tab_layout = new QVBoxLayout(m_preview_tab);
    preview_tab_layout->setContentsMargins(0, 0, 0, 0);
    preview_tab_layout->setSpacing(8);
    m_nested_image = new QLabel(m_nested_panel);
    m_nested_image->setAlignment(Qt::AlignCenter);
    m_nested_image->setBackgroundRole(QPalette::Base);
    m_nested_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_nested_image_scroll = new QScrollArea(m_nested_panel);
    m_nested_image_scroll->setWidget(m_nested_image);
    m_nested_image_scroll->setWidgetResizable(true);
    m_nested_image_scroll->hide();
    m_nested_entry_view = new QTreeView(m_nested_panel);
    m_nested_entry_view->setObjectName(QStringLiteral("NestedEntryTree"));
    m_nested_entry_view->setModel(m_nested_entry_model);
    m_nested_entry_view->setAlternatingRowColors(false);
    m_nested_entry_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nested_entry_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nested_entry_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nested_entry_view->setUniformRowHeights(true);
    m_nested_entry_view->setAnimated(false);
    m_nested_entry_view->setMouseTracking(true);
    m_nested_entry_view->setRootIsDecorated(true);
    m_nested_entry_view->setItemDelegate(new EntryTreeDelegate(m_entry_filter, m_file_filter, m_nested_entry_view));
    m_nested_entry_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nested_entry_view->header()->setStretchLastSection(false);
    m_nested_entry_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_nested_entry_view->header()->setMinimumSectionSize(64);
    m_nested_entry_view->header()->resizeSection(0, 240);
    m_nested_entry_view->setColumnWidth(1, 110);
    m_nested_entry_view->setColumnWidth(2, 110);
    m_nested_entry_view->setColumnWidth(3, 90);
    m_nested_entry_view->setColumnWidth(4, 180);
    m_nested_entry_view->hide();
    m_nested_body = new QPlainTextEdit(m_nested_panel);
    m_nested_body->setReadOnly(true);
    m_nested_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_nested_body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_nested_body->setMinimumWidth(0);
    m_video_container = new QWidget(m_nested_panel);
    m_video_container->setObjectName(QStringLiteral("VideoFrame"));
    m_video_container->setMinimumHeight(260);
    m_video_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_video_container->hide();
    auto* video_layout = new QVBoxLayout(m_video_container);
    video_layout->setContentsMargins(0, 0, 0, 0);
    video_layout->setSpacing(0);
    m_video_widget = new QVideoWidget(m_video_container);
    m_video_widget->setMinimumHeight(260);
    m_video_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_video_widget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_video_widget->hide();
    video_layout->addWidget(m_video_widget);
    m_audio_panel = new QWidget(m_nested_panel);
    m_audio_panel->setObjectName(QStringLiteral("AudioPanel"));
    auto* audio_layout = new QVBoxLayout(m_audio_panel);
    audio_layout->setContentsMargins(10, 8, 10, 8);
    audio_layout->setSpacing(6);
    m_mux_audio_row = new QWidget(m_audio_panel);
    auto* mux_audio_layout = new QHBoxLayout(m_mux_audio_row);
    mux_audio_layout->setContentsMargins(0, 0, 0, 0);
    mux_audio_layout->setSpacing(8);
    auto* mux_audio_label = make_dim_label(QStringLiteral("Audio channel"), m_mux_audio_row);
    m_mux_audio_combo = new QComboBox(m_mux_audio_row);
    m_mux_audio_combo->setObjectName(QStringLiteral("MuxAudioCombo"));
    m_mux_audio_combo->setToolTip(QStringLiteral("Choose which stream to mux with the video preview"));
    auto* mux_audio_button = new QToolButton(m_mux_audio_row);
    mux_audio_button->setArrowType(Qt::DownArrow);
    mux_audio_button->setToolTip(QStringLiteral("Show mux audio choices"));
    mux_audio_button->setAccessibleName(QStringLiteral("Show mux audio choices"));
    mux_audio_layout->addWidget(mux_audio_label, 0);
    mux_audio_layout->addWidget(m_mux_audio_combo, 1);
    mux_audio_layout->addWidget(mux_audio_button, 0);
    connect(mux_audio_button, &QToolButton::clicked, m_mux_audio_combo, &QComboBox::showPopup);
    m_mux_subtitle_row = new QWidget(m_audio_panel);
    auto* mux_subtitle_layout = new QHBoxLayout(m_mux_subtitle_row);
    mux_subtitle_layout->setContentsMargins(0, 0, 0, 0);
    mux_subtitle_layout->setSpacing(8);
    auto* mux_subtitle_label = make_dim_label(QStringLiteral("Subtitles"), m_mux_subtitle_row);
    m_mux_subtitle_combo = new QComboBox(m_mux_subtitle_row);
    m_mux_subtitle_combo->setObjectName(QStringLiteral("MuxSubtitleCombo"));
    m_mux_subtitle_combo->setToolTip(QStringLiteral("Choose which subtitle language to display"));
    auto* mux_subtitle_button = new QToolButton(m_mux_subtitle_row);
    mux_subtitle_button->setArrowType(Qt::DownArrow);
    mux_subtitle_button->setToolTip(QStringLiteral("Show mux subtitle choices"));
    mux_subtitle_button->setAccessibleName(QStringLiteral("Show mux subtitle choices"));
    mux_subtitle_layout->addWidget(mux_subtitle_label, 0);
    mux_subtitle_layout->addWidget(m_mux_subtitle_combo, 1);
    mux_subtitle_layout->addWidget(mux_subtitle_button, 0);
    connect(mux_subtitle_button, &QToolButton::clicked, m_mux_subtitle_combo, &QComboBox::showPopup);
    auto* audio_top = new QHBoxLayout();
    audio_top->setContentsMargins(0, 0, 0, 0);
    audio_top->setSpacing(8);
    m_audio_play_button = new QToolButton(m_audio_panel);
    m_audio_play_button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_audio_play_button->setText(QStringLiteral("Play"));
    m_audio_play_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_audio_play_button->setEnabled(false);
    m_audio_status_label = new QLabel(QStringLiteral("No playable audio selected"), m_audio_panel);
    m_audio_status_label->setObjectName(QStringLiteral("AudioStatus"));
    m_audio_status_label->setWordWrap(true);
    m_audio_volume_label = new QLabel(m_audio_panel);
    m_audio_volume_label->setPixmap(style()->standardIcon(QStyle::SP_MediaVolume).pixmap(16, 16));
    m_audio_volume_label->setToolTip(QStringLiteral("Volume"));
    m_audio_volume_label->setAccessibleName(QStringLiteral("Volume"));
    m_audio_volume_slider = new QSlider(Qt::Horizontal, m_audio_panel);
    m_audio_volume_slider->setObjectName(QStringLiteral("VolumeSlider"));
    m_audio_volume_slider->setRange(0, 100);
    m_audio_volume_slider->setValue(80);
    m_audio_volume_slider->setFixedWidth(96);
    m_audio_volume_slider->setToolTip(QStringLiteral("Playback volume"));
    m_audio_volume_slider->setAccessibleName(QStringLiteral("Playback volume"));
    audio_top->addWidget(m_audio_play_button, 0);
    audio_top->addWidget(m_audio_status_label, 1);
    audio_top->addWidget(m_audio_volume_label, 0, Qt::AlignVCenter);
    audio_top->addWidget(m_audio_volume_slider, 0, Qt::AlignVCenter);
    m_audio_loop_row = new QWidget(m_audio_panel);
    auto* loop_layout = new QVBoxLayout(m_audio_loop_row);
    loop_layout->setContentsMargins(0, 0, 0, 0);
    loop_layout->setSpacing(4);
    auto* loop_header = new QHBoxLayout();
    loop_header->setContentsMargins(0, 0, 0, 0);
    loop_header->setSpacing(8);
    m_audio_loop_toggle = new QCheckBox(QStringLiteral("Loop selected range"), m_audio_loop_row);
    m_audio_loop_toggle->setEnabled(false);
    loop_header->addWidget(m_audio_loop_toggle, 0);
    loop_header->addStretch(1);
    m_audio_loop_list = new QListWidget(m_audio_loop_row);
    m_audio_loop_list->setObjectName(QStringLiteral("LoopList"));
    m_audio_loop_list->setEnabled(false);
    m_audio_loop_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_audio_loop_list->setAlternatingRowColors(false);
    m_audio_loop_list->setUniformItemSizes(false);
    m_audio_loop_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_audio_loop_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_audio_loop_list->setMinimumHeight(42);
    m_audio_loop_list->setMaximumHeight(96);
    loop_layout->addLayout(loop_header);
    loop_layout->addWidget(m_audio_loop_list);
    auto* audio_bottom = new QHBoxLayout();
    audio_bottom->setContentsMargins(0, 0, 0, 0);
    audio_bottom->setSpacing(8);
    m_audio_progress = new SeekSlider(Qt::Horizontal, m_audio_panel);
    m_audio_progress->setRange(0, 0);
    m_audio_progress->setEnabled(false);
    m_audio_time_label = new QLabel(QStringLiteral("0:00 / 0:00"), m_audio_panel);
    m_audio_time_label->setMinimumWidth(92);
    m_audio_time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    audio_bottom->addWidget(m_audio_progress, 1);
    audio_bottom->addWidget(m_audio_time_label, 0);
    audio_layout->addWidget(m_mux_audio_row);
    audio_layout->addWidget(m_mux_subtitle_row);
    audio_layout->addLayout(audio_top);
    audio_layout->addWidget(m_audio_loop_row);
    audio_layout->addLayout(audio_bottom);
    m_audio_loop_row->hide();
    m_mux_audio_row->hide();
    m_mux_subtitle_row->hide();
    m_audio_panel->hide();
    m_raw_body = new QPlainTextEdit(m_preview_tabs);
    m_raw_body->setReadOnly(true);
    m_raw_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_raw_body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_raw_body->setMinimumWidth(560);
    m_raw_body->hide();
    m_raw_hex = new HexPreviewWidget(m_preview_tabs);
    preview_tab_layout->addWidget(m_video_container, 8);
    preview_tab_layout->addWidget(m_audio_panel);
    preview_tab_layout->addWidget(m_nested_entry_view, 8);
    preview_tab_layout->addWidget(m_nested_image_scroll, 8);
    preview_tab_layout->addWidget(m_nested_body, 8);
    preview_tab_layout->addStretch(1);
    m_preview_tabs->addTab(m_preview_tab, QStringLiteral("Preview"));
    m_preview_tabs->addTab(m_raw_hex, QStringLiteral("Raw"));
    m_preview_tabs->setTabEnabled(1, false);
    nested_layout->addWidget(m_nested_title);
    nested_layout->addWidget(m_nested_subtitle);
    auto* preview_actions = new QHBoxLayout();
    preview_actions->setContentsMargins(0, 0, 0, 0);
    preview_actions->setSpacing(8);
    preview_actions->addWidget(m_preview_extract_button, 0);
    preview_actions->addWidget(m_preview_extract_raw_button, 0);
    preview_actions->addWidget(m_preview_recover_key_button, 0);
    preview_actions->addWidget(m_preview_recover_usm_key_button, 0);
    preview_actions->addWidget(m_preview_recover_adx_key_button, 0);
    preview_actions->addWidget(m_preview_recover_aac_key_button, 0);
    preview_actions->addStretch(1);
    nested_layout->addLayout(preview_actions);
    nested_layout->addWidget(m_nested_info_panel);
    nested_layout->addWidget(m_preview_key_panel);
    nested_layout->addWidget(m_preview_tabs, 8);
    nested_layout->addStretch(1);
    m_nested_title->hide();
    m_nested_subtitle->hide();
    m_nested_info_panel->hide();
    m_preview_tabs->hide();
    m_video_container->hide();
    m_video_widget->hide();
    m_nested_entry_view->hide();
    m_nested_body->hide();
    m_nested_panel->setMaximumWidth(0);

    auto* content_host = new ContentHostWidget(this);
    m_content_host = content_host;
    m_content_host->setObjectName(QStringLiteral("AppChrome"));
    auto* host_layout = new QHBoxLayout(m_content_host);
    host_layout->setContentsMargins(0, 0, 0, 0);
    host_layout->setSpacing(0);
    m_left_edge_rail = new AutoHideRail(m_content_host);
    m_left_edge_rail->setObjectName(QStringLiteral("SideRail"));
    auto* left_rail_layout = new QVBoxLayout(m_left_edge_rail);
    left_rail_layout->setContentsMargins(4, 8, 4, 0);
    left_rail_layout->setSpacing(6);
    left_rail_layout->addWidget(m_left_panel_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    left_rail_layout->addWidget(m_clear_files_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    left_rail_layout->addStretch(1);
    auto* left_hover_band = new EdgeHoverBand([this] {
        if (m_left_edge_rail != nullptr) {
            auto* rail = static_cast<AutoHideRail*>(m_left_edge_rail);
            rail->reveal();
        }
    }, m_content_host);
    if (m_left_edge_rail != nullptr) {
        auto* rail = static_cast<AutoHideRail*>(m_left_edge_rail);
        rail->set_expanded_changed([left_hover_band](bool expanded) {
            left_hover_band->setVisible(!expanded);
            if (!expanded) {
                left_hover_band->raise();
            }
        });
    }
    m_splitter = new ColumnSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(15);
    m_splitter->addWidget(m_left_panel);
    m_splitter->addWidget(right);
    m_splitter->addWidget(m_nested_panel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_toggle_preview_action != nullptr && m_toggle_preview_action->isChecked()) {
            const auto sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[2] >= 320) {
                m_preview_panel_width = sizes[2];
            }
        }
        schedule_position_edge_buttons();
    });
    m_right_edge_rail = new AutoHideRail(m_content_host);
    m_right_edge_rail->setObjectName(QStringLiteral("PreviewRail"));
    auto* right_rail_layout = new QVBoxLayout(m_right_edge_rail);
    right_rail_layout->setContentsMargins(4, 8, 4, 0);
    right_rail_layout->setSpacing(0);
    right_rail_layout->addWidget(m_preview_panel_button, 0, Qt::AlignHCenter | Qt::AlignTop);
    right_rail_layout->addStretch(1);
    auto* right_hover_band = new EdgeHoverBand([this] {
        if (m_right_edge_rail != nullptr) {
            auto* rail = static_cast<AutoHideRail*>(m_right_edge_rail);
            rail->reveal();
        }
    }, m_content_host);
    if (m_right_edge_rail != nullptr) {
        auto* rail = static_cast<AutoHideRail*>(m_right_edge_rail);
        rail->set_hover_guard(m_nested_panel);
        rail->set_expanded_changed([right_hover_band](bool expanded) {
            right_hover_band->setVisible(!expanded);
            if (!expanded) {
                right_hover_band->raise();
            }
        });
    }
    host_layout->addWidget(m_left_edge_rail);
    host_layout->addWidget(m_splitter, 1);
    host_layout->addWidget(m_right_edge_rail);
    content_host->set_edge_hover_bands(left_hover_band, right_hover_band);
    m_splitter->setSizes({320, 1120, 0});

    m_editor_workspace = new EditorWorkspace(this);
    m_workspace_tabs = new WorkspaceTabWidget(this);
    m_workspace_tabs->setObjectName(QStringLiteral("WorkspaceTabs"));
    m_workspace_tabs->setDocumentMode(false);
    m_workspace_tabs->addTab(m_content_host, QStringLiteral("Browse"));
    m_workspace_tabs->addTab(m_editor_workspace, QStringLiteral("Editor"));
    connect(m_workspace_tabs, &QTabWidget::currentChanged, this, [this, content_host = m_content_host](int index) {
        if (m_workspace_tabs->widget(index) != content_host) {
            reset_audio_preview();
        }
    });
    setCentralWidget(m_workspace_tabs);

    m_drop_overlay = new QFrame(this);
    m_drop_overlay->setObjectName(QStringLiteral("DropOverlay"));
    m_drop_overlay->hide();
    m_drop_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* drop_layout = new QVBoxLayout(m_drop_overlay);
    drop_layout->setContentsMargins(28, 28, 28, 28);
    auto* drop_label = new QLabel(QStringLiteral("Drop files or folders"), m_drop_overlay);
    drop_label->setObjectName(QStringLiteral("DropOverlayLabel"));
    drop_label->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(drop_label, 1);

    const std::array<QWidget*, 10> drop_targets = {
        m_workspace_tabs,
        m_content_host,
        m_left_panel,
        m_file_view,
        m_file_view->viewport(),
        m_entry_view,
        m_entry_view->viewport(),
        m_nested_panel,
        m_nested_entry_view,
        m_nested_entry_view->viewport()
    };
    for (auto* target : drop_targets) {
        target->setAcceptDrops(true);
        target->installEventFilter(this);
    }
    schedule_position_edge_buttons();

    m_loading_bar = new QProgressBar(this);
    m_loading_bar->setRange(0, 0);
    m_loading_bar->setMaximumWidth(180);
    m_loading_bar->hide();
    m_loading_status_label = new QLabel(this);
    m_loading_status_label->setObjectName(QStringLiteral("LoadingStatus"));
    m_loading_status_label->setMinimumWidth(220);
    m_loading_status_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_loading_status_label->hide();
    statusBar()->addPermanentWidget(m_loading_status_label);
    statusBar()->addPermanentWidget(m_loading_bar);

    m_work_timer = new QTimer(this);
    connect(m_work_timer, &QTimer::timeout, this, &MainWindow::poll_background_work);
    m_load_watcher = new QFutureWatcher<LoadResult>(this);
    connect(m_load_watcher, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::consume_load_result);
    m_extract_watcher = new QFutureWatcher<ExtractionReport>(this);
    connect(m_extract_watcher, &QFutureWatcher<ExtractionReport>::finished, this, &MainWindow::consume_extract_result);
    m_materialize_watcher = new QFutureWatcher<MaterializeResult>(this);
    connect(m_materialize_watcher, &QFutureWatcher<MaterializeResult>::finished, this, &MainWindow::consume_materialize_result);
    m_preview_watcher = new QFutureWatcher<PreviewResult>(this);
    connect(m_preview_watcher, &QFutureWatcher<PreviewResult>::finished, this, &MainWindow::consume_preview_result);
    m_hca_key_recovery_watcher = new QFutureWatcher<HcaKeyRecoveryTaskResult>(this);
    connect(
        m_hca_key_recovery_watcher,
        &QFutureWatcher<HcaKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_hca_key_recovery_result);
    m_usm_key_recovery_watcher = new QFutureWatcher<UsmKeyRecoveryTaskResult>(this);
    connect(
        m_usm_key_recovery_watcher,
        &QFutureWatcher<UsmKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_usm_key_recovery_result);
    m_adx_key_recovery_watcher = new QFutureWatcher<AdxKeyRecoveryTaskResult>(this);
    connect(
        m_adx_key_recovery_watcher,
        &QFutureWatcher<AdxKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_adx_key_recovery_result);
    m_aac_key_recovery_watcher = new QFutureWatcher<AacKeyRecoveryTaskResult>(this);
    connect(
        m_aac_key_recovery_watcher,
        &QFutureWatcher<AacKeyRecoveryTaskResult>::finished,
        this,
        &MainWindow::consume_aac_key_recovery_result);
    connect(m_file_filter, &QLineEdit::textChanged, file_proxy, [this, file_proxy](const QString& text) {
        file_proxy->set_search_text(text);
        update_file_list_status();
    });
    connect(m_file_filter, &QLineEdit::textChanged, this, &MainWindow::update_file_list_status);
    connect(m_file_filter, &QLineEdit::textChanged, m_file_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_filter, &QLineEdit::textChanged, m_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_filter, &QLineEdit::textChanged, m_nested_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_file_sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        update_file_sort();
        update_file_list_status();
    });
    static_cast<FileTypeFilterCombo*>(m_file_type_filter)->set_selection_changed([this, file_proxy] {
        update_file_type_filter_label(m_file_type_filter);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
        update_file_list_status();
    });
    connect(m_file_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_loaded_file_context_menu);
    connect(m_entry_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_entry_context_menu);
    connect(m_nested_entry_view, &QWidget::customContextMenuRequested, this, &MainWindow::show_nested_entry_context_menu);
    connect(m_doc_extract_button, &QToolButton::clicked, this, [this] {
        auto targets = selected_file_targets();
        auto mux_audio_choice = targets.size() == 1 ? current_mux_audio_choice() : std::nullopt;
        start_extraction(
            std::move(targets),
            ExtractionMode::Decoded,
            mux_audio_choice
        );
    });
    connect(m_doc_extract_raw_button, &QToolButton::clicked, this, [this] {
        start_extraction(selected_file_targets(), ExtractionMode::Raw);
    });
    connect(m_preview_extract_button, &QToolButton::clicked, this, [this] {
        start_extraction(current_preview_entry_targets(), ExtractionMode::Decoded);
    });
    connect(m_preview_extract_raw_button, &QToolButton::clicked, this, [this] {
        start_extraction(current_preview_entry_targets(), ExtractionMode::Raw);
    });
    connect(m_preview_recover_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QStringLiteral("Previewed file");
        start_hca_key_recovery(std::move(sources), label);
    });
    connect(m_preview_recover_usm_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_usm_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QStringLiteral("Previewed file");
        start_usm_key_recovery(std::move(sources), label);
    });
    connect(m_preview_recover_adx_key_button, &QToolButton::clicked, this, [this] {
        const auto kind = current_preview_adx_recovery_kind();
        if (!kind) {
            return;
        }
        auto sources = current_preview_adx_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QStringLiteral("Previewed file");
        start_adx_key_recovery(std::move(sources), *kind, label);
    });
    connect(m_preview_recover_aac_key_button, &QToolButton::clicked, this, [this] {
        auto sources = current_preview_aac_recovery_sources();
        const auto label = m_current_preview_entry.has_value()
            ? utf8_to_qstring(m_current_preview_entry->name)
            : QStringLiteral("Previewed ACB/AWB");
        start_aac_key_recovery(std::move(sources), label);
    });
    connect(m_preview_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0 && !m_current_preview_entry.has_value() && !m_preview_running &&
            m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
            m_file_view->currentIndex().isValid()) {
            const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (const auto* document = ensure_loaded_document(source.row()); document != nullptr && is_mux_document(*document)) {
                start_document_mux_preview(*document, 0);
            }
            return;
        }
        if (index != 1 || m_current_preview_entry.has_value() || m_preview_running ||
            m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr ||
            !m_file_view->currentIndex().isValid()) {
            return;
        }
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = ensure_loaded_document(source.row()); document != nullptr) {
            populate_document_raw_tab(*document, true);
        }
    });
    connect(m_file_proxy, &QAbstractItemModel::rowsInserted, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::rowsRemoved, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::modelReset, this, &MainWindow::update_file_list_status);
    connect(m_file_proxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::update_file_list_status);
    connect(m_file_model, &QAbstractItemModel::rowsInserted, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::rowsRemoved, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::modelReset, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_file_model, &QAbstractItemModel::dataChanged, this, [this, file_proxy] {
        refresh_file_type_filter(m_file_type_filter, m_file_model);
        file_proxy->set_type_filters(selected_file_type_filters(m_file_type_filter));
    });
    connect(m_entry_filter, &QLineEdit::textChanged, m_entry_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_entry_filter, &QLineEdit::textChanged, m_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_entry_filter, &QLineEdit::textChanged, m_nested_entry_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_entry_view, &QTreeView::doubleClicked, this, [this](const QModelIndex&) {
        activate_current_entry();
    });
    connect(m_entry_up_button, &QToolButton::clicked, this, [this] {
        if (m_entry_model != nullptr && m_entry_model->flat_can_go_up()) {
            set_entry_list_path(QString::fromStdString(m_entry_model->flat_parent_path()));
        }
    });
    connect(m_entry_path_label, &QLabel::linkActivated, this, [this](const QString& path) {
        set_entry_list_path(path == QStringLiteral("@root") ? QString{} : path);
    });
    connect(m_entry_view_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        sync_segment_buttons(m_entry_view_mode->parentWidget(), index);
        if (m_entry_model == nullptr || m_entry_view == nullptr) {
            return;
        }
        const auto flat = index == 1;
        if (flat && m_entry_model->has_custom_columns()) {
            return;
        }
        m_entry_model->set_flat_mode(flat);
        m_entry_view->setRootIsDecorated(!flat && !m_entry_model->has_custom_columns());
        update_entry_path_bar();
        fit_entry_columns(m_entry_view, m_entry_model->has_custom_columns());
        if (!flat && !m_entry_model->has_custom_columns()) {
            m_entry_view->expandToDepth(6);
        }
    });
    connect(m_audio_play_button, &QToolButton::clicked, this, [this] {
        if (m_audio_player == nullptr || m_audio_source_path.isEmpty()) {
            return;
        }
        if (m_audio_player->playbackState() == QMediaPlayer::PlayingState) {
            m_audio_player->pause();
        } else {
            m_audio_player->play();
        }
    });
    connect(m_audio_progress, &QSlider::sliderPressed, this, [this] {
        m_audio_slider_dragging = true;
    });
    connect(m_audio_progress, &QSlider::sliderReleased, this, [this] {
        m_audio_slider_dragging = false;
        if (m_audio_player != nullptr) {
            m_audio_player->setPosition(m_audio_progress->value());
        }
    });
    connect(m_audio_progress, &QSlider::sliderMoved, this, [this](int value) {
        if (m_audio_player != nullptr) {
            m_audio_player->setPosition(value);
        }
    });
    connect(m_audio_volume_slider, &QSlider::valueChanged, this, [this](int value) {
        if (m_audio_output != nullptr) {
            m_audio_output->setVolume(static_cast<float>(std::clamp(value, 0, 100)) / 100.0f);
        }
    });
    connect(m_audio_loop_toggle, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && m_audio_loop_list != nullptr && m_audio_loop_list->currentRow() < 0 && !m_audio_loops.empty()) {
            m_audio_loop_list->setCurrentRow(0);
        }
    });
    connect(m_mux_audio_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0 || m_preview_running || m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr) {
            return;
        }
        const auto current = m_file_view->currentIndex();
        if (!current.isValid()) {
            return;
        }
        const auto source = m_file_proxy->mapToSource(current);
        const auto* document = ensure_loaded_document(source.row());
        if (document == nullptr) {
            return;
        }
        start_document_mux_preview(*document, m_mux_audio_combo->currentData().toInt());
    });
    connect(m_mux_subtitle_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_audio_player == nullptr || index < 0) {
            return;
        }
        m_audio_player->setActiveSubtitleTrack(m_mux_subtitle_combo->currentData().toInt());
    });
    connect(m_file_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            const auto source = m_file_proxy->mapToSource(current);
            select_document(source.row());
            m_skip_next_file_click_reload = QApplication::mouseButtons() != Qt::NoButton;
        });
    connect(m_file_view, &QListView::clicked, this, [this](const QModelIndex& current) {
        if (!current.isValid()) {
            return;
        }
        if (m_entry_view != nullptr && m_entry_view->selectionModel() != nullptr) {
            m_entry_view->selectionModel()->clear();
            m_entry_view->selectionModel()->clearCurrentIndex();
        }
        if (m_nested_entry_view != nullptr && m_nested_entry_view->selectionModel() != nullptr) {
            m_nested_entry_view->selectionModel()->clear();
            m_nested_entry_view->selectionModel()->clearCurrentIndex();
        }
        if (m_skip_next_file_click_reload) {
            m_skip_next_file_click_reload = false;
            return;
        }
        const auto source = m_file_proxy->mapToSource(current);
        select_document(source.row());
    });
    connect(m_file_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this] {
            update_file_list_status();
        });
    connect(m_doc_key_apply, &QToolButton::clicked, this, [this] {
        apply_key_panel_value(
            m_doc_key_kind,
            m_doc_key_input == nullptr ? QString{} : m_doc_key_input->text(),
            key_base_value(m_doc_key_base_input)
        );
    });
    connect(m_doc_key_input, &QLineEdit::returnPressed, this, [this] {
        apply_key_panel_value(
            m_doc_key_kind,
            m_doc_key_input == nullptr ? QString{} : m_doc_key_input->text(),
            key_base_value(m_doc_key_base_input)
        );
    });
    connect(m_preview_key_apply, &QToolButton::clicked, this, [this] {
        apply_key_panel_value(
            m_preview_key_kind,
            m_preview_key_input == nullptr ? QString{} : m_preview_key_input->text(),
            key_base_value(m_preview_key_base_input)
        );
    });
    connect(m_preview_key_input, &QLineEdit::returnPressed, this, [this] {
        apply_key_panel_value(
            m_preview_key_kind,
            m_preview_key_input == nullptr ? QString{} : m_preview_key_input->text(),
            key_base_value(m_preview_key_base_input)
        );
    });
    connect(m_doc_mux_preview_button, &QToolButton::clicked, this, [this] {
        if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || !m_file_view->currentIndex().isValid()) {
            return;
        }
        if (m_entry_view != nullptr) {
            m_entry_view->clearSelection();
            if (auto* selection = m_entry_view->selectionModel(); selection != nullptr) {
                selection->clearCurrentIndex();
            }
        }
        const auto file_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = ensure_loaded_document(file_source.row()); document != nullptr && is_mux_document(*document)) {
            start_document_mux_preview(*document, 0);
        }
    });

    connect(m_entry_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            if (!current.isValid()) {
                return;
            }

            const auto source = m_entry_proxy->mapToSource(current);
            if (const auto* summary = m_entry_model->summary_at(source); summary != nullptr) {
                if (summary->has_source) {
                    start_entry_preview(*summary);
                } else if (!summary->inspector_entries.empty()) {
                    show_entry_inspector(*summary);
                }
            }
        });
    connect(m_entry_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this] {
            update_entry_selection_status();
        });
    connect(m_nested_entry_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) {
            if (!current.isValid()) {
                return;
            }

            if (const auto* summary = m_nested_entry_model->summary_at(current); summary != nullptr) {
                if (summary->has_source) {
                    start_entry_preview(*summary);
                } else if (!summary->inspector_entries.empty()) {
                    show_entry_inspector(*summary);
                }
            }
        });

    statusBar()->showMessage(QStringLiteral("Ready"));
    update_file_sort();
    update_file_list_status();
}

void MainWindow::build_menus() {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open_files_action = file_menu->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("&Open Files..."));
    open_files_action->setShortcut(QKeySequence::Open);
    connect(open_files_action, &QAction::triggered, this, &MainWindow::open_files);

    auto* open_folder_action = file_menu->addAction(QStringLiteral("Open &Folder..."));
    connect(open_folder_action, &QAction::triggered, this, &MainWindow::open_folder);

    file_menu->addSeparator();
    auto* new_menu = file_menu->addMenu(QStringLiteral("&New"));
    auto* new_utf_action = new_menu->addAction(QStringLiteral("&UTF Table"));
    connect(new_utf_action, &QAction::triggered, this, &MainWindow::new_utf_editor_document);
    auto* new_afs_action = new_menu->addAction(QStringLiteral("&AFS Archive"));
    connect(new_afs_action, &QAction::triggered, this, &MainWindow::new_afs_editor_document);
    auto* new_awb_action = new_menu->addAction(QStringLiteral("A&WB/AFS2 Archive"));
    connect(new_awb_action, &QAction::triggered, this, &MainWindow::new_awb_editor_document);
    auto* new_acx_action = new_menu->addAction(QStringLiteral("A&CX Archive"));
    connect(new_acx_action, &QAction::triggered, this, &MainWindow::new_acx_editor_document);
    auto* new_cpk_action = new_menu->addAction(QStringLiteral("C&PK Archive"));
    connect(new_cpk_action, &QAction::triggered, this, &MainWindow::new_cpk_editor_document);
    new_menu->addSeparator();
    auto* new_audio_encode_action = new_menu->addAction(QStringLiteral("Audio &Encode Job"));
    connect(new_audio_encode_action, &QAction::triggered, this, &MainWindow::new_audio_encode_document);
    auto* new_usm_build_action = new_menu->addAction(QStringLiteral("&USM Movie..."));
    connect(new_usm_build_action, &QAction::triggered, this, &MainWindow::new_media_build_document);
    auto* new_sfd_build_action = new_menu->addAction(QStringLiteral("S&FD Movie..."));
    connect(new_sfd_build_action, &QAction::triggered, this, &MainWindow::new_sfd_build_document);
    auto* new_aax_build_action = new_menu->addAction(QStringLiteral("&AAX From ADX..."));
    connect(new_aax_build_action, &QAction::triggered, this, &MainWindow::new_aax_build_document);
    auto* new_aix_build_action = new_menu->addAction(QStringLiteral("A&IX From ADX..."));
    connect(new_aix_build_action, &QAction::triggered, this, &MainWindow::new_aix_build_document);
    auto* new_csb_build_action = new_menu->addAction(QStringLiteral("&CSB From Folder..."));
    connect(new_csb_build_action, &QAction::triggered, this, &MainWindow::new_csb_build_document);
    auto* new_cvm_script_action = new_menu->addAction(QStringLiteral("CVM From C&VS..."));
    connect(new_cvm_script_action, &QAction::triggered, this, &MainWindow::new_cvm_from_script_document);
    auto* new_cvm_directory_action = new_menu->addAction(QStringLiteral("CVM From F&older..."));
    connect(new_cvm_directory_action, &QAction::triggered, this, &MainWindow::new_cvm_from_directory_document);

    file_menu->addSeparator();
    auto* extract_all_action = file_menu->addAction(make_action_icon(ActionGlyph::Extract), QStringLiteral("Extract &All..."));
    connect(extract_all_action, &QAction::triggered, this, [this] {
        start_extraction(all_file_targets(), ExtractionMode::Decoded);
    });
    auto* extract_all_raw_action = file_menu->addAction(make_action_icon(ActionGlyph::RawExtract), QStringLiteral("Extract All &Raw..."));
    connect(extract_all_raw_action, &QAction::triggered, this, [this] {
        start_extraction(all_file_targets(), ExtractionMode::Raw);
    });
    auto* recover_hca_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QStringLiteral("Recover HCA Keys from All Loaded Files"));
    connect(recover_hca_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_recovery_sources();
        const auto count = sources.size();
        start_hca_key_recovery(
            std::move(sources),
            QStringLiteral("%1 loaded file%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    });
    auto* recover_aac_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QStringLiteral("Recover AAC Keys from Loaded ACB/AWB Files"));
    connect(recover_aac_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_aac_recovery_sources();
        const auto count = sources.size();
        start_aac_key_recovery(
            std::move(sources),
            QStringLiteral("%1 loaded ACB/AWB file%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    });
    auto* recover_usm_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QStringLiteral("Recover USM Keys from All Loaded Files"));
    connect(recover_usm_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_usm_recovery_sources();
        const auto count = sources.size();
        start_usm_key_recovery(
            std::move(sources),
            QStringLiteral("%1 loaded file%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    });
    auto* recover_adx_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QStringLiteral("Recover ADX Keys from All Loaded Files"));
    connect(recover_adx_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            AdxRecoveryKind::Adx,
            QStringLiteral("%1 loaded file%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    });
    auto* recover_ahx_keys_action = file_menu->addAction(
        make_action_icon(ActionGlyph::RecoverKey),
        QStringLiteral("Recover AHX Keys from All Loaded Files"));
    connect(recover_ahx_keys_action, &QAction::triggered, this, [this] {
        auto sources = all_file_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            AdxRecoveryKind::Ahx,
            QStringLiteral("%1 loaded file%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    });

    file_menu->addSeparator();
    auto* clear_action = file_menu->addAction(make_action_icon(ActionGlyph::Clear), QStringLiteral("&Clear Loaded Files"));
    connect(clear_action, &QAction::triggered, this, &MainWindow::clear_loaded_files);

    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QStringLiteral("E&xit"));
    exit_action->setShortcut(QKeySequence::Quit);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    m_edit_menu = menuBar()->addMenu(QStringLiteral("&Edit"));
    m_decryption_keys_action = m_edit_menu->addAction(QStringLiteral("&Cryptography Keys"));
    connect(m_decryption_keys_action, &QAction::triggered, this, &MainWindow::show_decryption_keys_panel);
    m_edit_menu->addSeparator();
    m_extract_mux_outputs_action = m_edit_menu->addAction(QStringLiteral("Extract USM/SFD &Mux Outputs"));
    m_extract_mux_outputs_action->setCheckable(true);
    m_extract_mux_outputs_action->setChecked(m_allow_mux_extract_outputs);
    connect(m_extract_mux_outputs_action, &QAction::toggled, this, [this](bool checked) {
        m_allow_mux_extract_outputs = checked;
    });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* theme_group = new QActionGroup(this);
    m_light_theme_action = view_menu->addAction(QStringLiteral("&Light Mode"));
    m_dark_theme_action = view_menu->addAction(QStringLiteral("&Dark Mode"));
    m_light_theme_action->setCheckable(true);
    m_dark_theme_action->setCheckable(true);
    theme_group->addAction(m_light_theme_action);
    theme_group->addAction(m_dark_theme_action);
    m_light_theme_action->setChecked(m_theme == Theme::Light);
    m_dark_theme_action->setChecked(m_theme == Theme::Dark);
    connect(m_light_theme_action, &QAction::triggered, this, [this] { set_theme(Theme::Light); });
    connect(m_dark_theme_action, &QAction::triggered, this, [this] { set_theme(Theme::Dark); });
    view_menu->addSeparator();
    m_compact_lists_action = view_menu->addAction(QStringLiteral("&Compact Lists"));
    m_compact_lists_action->setCheckable(true);
    QSettings ui_settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
    m_compact_lists_action->setChecked(ui_settings.value(QStringLiteral("ui/compactLists"), false).toBool());
    connect(m_compact_lists_action, &QAction::toggled, this, &MainWindow::set_compact_lists);
    set_compact_lists(m_compact_lists_action->isChecked());

    auto* window_menu = menuBar()->addMenu(QStringLiteral("&Window"));
    m_toggle_left_action = window_menu->addAction(make_sidebar_icon(true), QStringLiteral("Loaded Files Panel"));
    m_toggle_left_action->setCheckable(true);
    m_toggle_left_action->setChecked(true);
    connect(m_toggle_left_action, &QAction::triggered, this, [this](bool checked) {
        if (m_left_panel_button != nullptr) {
            m_left_panel_button->setChecked(checked);
        }
        toggle_left_panel();
    });

    m_toggle_preview_action = window_menu->addAction(make_sidebar_icon(false), QStringLiteral("Entry Preview Panel"));
    m_toggle_preview_action->setCheckable(true);
    m_toggle_preview_action->setChecked(false);
    connect(m_toggle_preview_action, &QAction::triggered, this, [this](bool checked) {
        if (m_preview_panel_button != nullptr) {
            m_preview_panel_button->setChecked(checked);
        }
        toggle_preview_panel();
    });

    window_menu->addSeparator();
    m_always_show_access_keys_action = window_menu->addAction(QStringLiteral("Always Show &Access Keys"));
    m_always_show_access_keys_action->setCheckable(true);
    m_always_show_access_keys_action->setChecked(qApp->property("alwaysShowAccessKeys").toBool());
    connect(m_always_show_access_keys_action, &QAction::toggled, this, [](bool visible) {
        qApp->setProperty("alwaysShowAccessKeys", visible);
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget != nullptr) {
                widget->update();
            }
        }
    });

    window_menu->addSeparator();
    auto* lock_left_shelf_action = window_menu->addAction(QStringLiteral("Lock Loaded Files Shelf"));
    lock_left_shelf_action->setCheckable(true);
    connect(lock_left_shelf_action, &QAction::triggered, this, [this](bool locked) {
        if (m_left_edge_rail != nullptr) {
            static_cast<AutoHideRail*>(m_left_edge_rail)->set_auto_hide_enabled(!locked);
        }
    });

    auto* lock_preview_shelf_action = window_menu->addAction(QStringLiteral("Lock Preview Shelf"));
    lock_preview_shelf_action->setCheckable(true);
    connect(lock_preview_shelf_action, &QAction::triggered, this, [this](bool locked) {
        if (m_right_edge_rail != nullptr) {
            static_cast<AutoHideRail*>(m_right_edge_rail)->set_auto_hide_enabled(!locked);
        }
    });

    auto* lock_workspace_ribbon_action = window_menu->addAction(QStringLiteral("Lock Browse/Editor Ribbon"));
    lock_workspace_ribbon_action->setCheckable(true);
    lock_workspace_ribbon_action->setChecked(true);
    connect(lock_workspace_ribbon_action, &QAction::triggered, this, [this](bool locked) {
        if (m_workspace_tabs != nullptr) {
            static_cast<WorkspaceTabWidget*>(m_workspace_tabs)->set_ribbon_locked(locked);
        }
    });
    if (m_workspace_tabs != nullptr) {
        static_cast<WorkspaceTabWidget*>(m_workspace_tabs)->set_ribbon_locked(true);
    }

    window_menu->addSeparator();
    auto* reset_layout_action = window_menu->addAction(QStringLiteral("&Reset Layout"));
    connect(reset_layout_action, &QAction::triggered, this, [this] {
        if (m_toggle_left_action != nullptr) {
            m_toggle_left_action->setChecked(true);
        }
        if (m_left_panel_button != nullptr) {
            m_left_panel_button->setChecked(true);
        }
        toggle_left_panel();
        const auto preview_open = m_preview_panel_button != nullptr && m_preview_panel_button->isChecked();
        if (m_toggle_preview_action != nullptr) {
            m_toggle_preview_action->setChecked(preview_open);
        }
        toggle_preview_panel();
        m_splitter->setSizes(preview_open ? QList<int>{320, 900, 640} : QList<int>{320, 1120, 0});
        QSettings settings(QStringLiteral("CriCodecs"), QStringLiteral("CriStudio"));
        settings.remove(QStringLiteral("ui"));
    });

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto* log_action = help_menu->addAction(QStringLiteral("Log File Location"));
    connect(log_action, &QAction::triggered, this, [this] {
        statusBar()->showMessage(log_path(), 8000);
    });
    auto* about_action = help_menu->addAction(QStringLiteral("&About CriStudio"));
    connect(about_action, &QAction::triggered, this, [this] {
        statusBar()->showMessage(app_title() + QStringLiteral(" uses the native CriCodecs core."), 8000);
    });

    m_memory_usage_label = new QLabel(this);
    m_memory_usage_label->setObjectName(QStringLiteral("MemoryUsageLabel"));
    m_memory_usage_label->setMinimumWidth(96);
    m_memory_usage_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_memory_usage_label->setToolTip(QStringLiteral("Resident process memory"));
    menuBar()->setCornerWidget(m_memory_usage_label, Qt::TopRightCorner);

    m_memory_usage_timer = new QTimer(this);
    connect(m_memory_usage_timer, &QTimer::timeout, this, &MainWindow::update_memory_usage_label);
    m_memory_usage_timer->start(2000);
    update_memory_usage_label();
}

void MainWindow::update_memory_usage_label() {
    if (m_memory_usage_label == nullptr) {
        return;
    }
    const auto bytes = resident_memory_bytes();
    if (!bytes) {
        m_memory_usage_label->setText(QStringLiteral("Mem --"));
        return;
    }
    m_memory_usage_label->setText(QStringLiteral("Mem %1").arg(format_memory_size(*bytes)));
}

void MainWindow::toggle_left_panel() {
    if (m_left_panel == nullptr || m_toggle_left_action == nullptr) {
        return;
    }
    const auto expanded = m_toggle_left_action->isChecked();
    m_file_filter->setVisible(expanded);
    if (m_file_sort != nullptr) {
        m_file_sort->setVisible(expanded);
    }
    if (m_file_type_filter != nullptr) {
        m_file_type_filter->setVisible(expanded);
    }
    m_file_view->setVisible(expanded);
    if (m_file_list_status != nullptr) {
        m_file_list_status->setVisible(expanded);
    }
    if (expanded) {
        m_left_panel->setMaximumWidth(QWIDGETSIZE_MAX);
        m_left_panel->setMinimumWidth(260);
        if (m_splitter != nullptr) {
            const auto sizes = m_splitter->sizes();
            if (!sizes.empty() && sizes[0] < 260) {
                m_splitter->setSizes({320, sizes.value(1, 900), sizes.value(2, 0)});
            }
        }
    } else {
        m_left_panel->setMinimumWidth(0);
        m_left_panel->setMaximumWidth(0);
    }
    schedule_position_edge_buttons();
}

void MainWindow::position_edge_buttons() {
    if (m_left_panel_button != nullptr) {
        m_left_panel_button->raise();
        m_left_panel_button->show();
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->raise();
        m_preview_panel_button->show();
    }
}

void MainWindow::schedule_position_edge_buttons() {
    position_edge_buttons();
    QTimer::singleShot(0, this, [this] {
        position_edge_buttons();
    });
}

void MainWindow::set_theme(Theme theme) {
    m_theme = theme;
    if (m_light_theme_action != nullptr) {
        const QSignalBlocker blocker(m_light_theme_action);
        m_light_theme_action->setChecked(theme == Theme::Light);
    }
    if (m_dark_theme_action != nullptr) {
        const QSignalBlocker blocker(m_dark_theme_action);
        m_dark_theme_action->setChecked(theme == Theme::Dark);
    }
    if (theme == Theme::Dark) {
        QApplication::setPalette(dark_palette());
        qApp->setStyleSheet(visual_stylesheet(true));
    } else {
        QApplication::setPalette(light_palette());
        qApp->setStyleSheet(visual_stylesheet(false));
    }

    const auto left_icon = make_sidebar_icon(true);
    const auto right_icon = make_sidebar_icon(false);
    const auto clear_icon = make_action_icon(ActionGlyph::Clear);
    const auto extract_icon = make_action_icon(ActionGlyph::Extract);
    const auto raw_icon = make_action_icon(ActionGlyph::RawExtract);
    const auto recover_key_icon = make_action_icon(ActionGlyph::RecoverKey);
    const auto mux_icon = make_action_icon(ActionGlyph::MuxPreview);
    if (m_left_panel_button != nullptr) {
        m_left_panel_button->setIcon(left_icon);
    }
    if (m_clear_files_button != nullptr) {
        m_clear_files_button->setIcon(clear_icon);
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->setIcon(right_icon);
    }
    if (m_doc_extract_button != nullptr) {
        m_doc_extract_button->setIcon(extract_icon);
    }
    if (m_doc_extract_raw_button != nullptr) {
        m_doc_extract_raw_button->setIcon(raw_icon);
    }
    if (m_doc_mux_preview_button != nullptr) {
        m_doc_mux_preview_button->setIcon(mux_icon);
    }
    if (m_preview_extract_button != nullptr) {
        m_preview_extract_button->setIcon(extract_icon);
    }
    if (m_preview_extract_raw_button != nullptr) {
        m_preview_extract_raw_button->setIcon(raw_icon);
    }
    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setIcon(recover_key_icon);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setIcon(recover_key_icon);
    }
    if (m_toggle_left_action != nullptr) {
        m_toggle_left_action->setIcon(left_icon);
    }
    if (m_toggle_preview_action != nullptr) {
        m_toggle_preview_action->setIcon(right_icon);
    }
}



} // namespace cristudio
