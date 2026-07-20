#include "entry_table_model.hpp"

#include "format_icon.hpp"
#include "path_text.hpp"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace cristudio {
namespace {

std::vector<std::string> split_entry_path(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < path.size()) {
        const auto separator = path.find_first_of("/\\", start);
        const auto end = separator == std::string::npos ? path.size() : separator;
        if (end > start) {
            parts.emplace_back(path.substr(start, end - start));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }

    if (parts.empty()) {
        parts.push_back(path.empty() ? std::string("(unnamed)") : path);
    }
    return parts;
}

std::string normalize_entry_path(std::string path) {
    std::ranges::replace(path, '\\', '/');
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

QString display_string(const std::string& value) {
    return utf8_to_qstring(value);
}

QString folded_display_string(const std::string& value) {
    return display_string(value).toCaseFolded();
}

qulonglong leading_number(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    qulonglong value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} ? value : 0;
}

bool is_numeric_column_type(std::string_view type) {
    return type == "u8" || type == "s8" ||
        type == "u16" || type == "s16" ||
        type == "u32" || type == "s32" ||
        type == "u64" || type == "s64" ||
        type == "float" || type == "double";
}

bool is_aix_summary(const EntrySummary& summary) {
    return summary.source_format == "AIX" || summary.type == "AIX";
}

QString indexed_sort_key(bool folder, const EntrySummary& summary) {
    return QString(folder ? QLatin1Char('0') : QLatin1Char('1')) +
        QStringLiteral("%1")
        .arg(static_cast<qulonglong>(summary.source_index), 12, 10, QLatin1Char('0'));
}

QVariant table_column_tint(std::string_view type) {
    QColor color;
    if (type == "string") {
        color = QColor(86, 156, 214);
    } else if (type == "binary") {
        color = QColor(197, 134, 192);
    } else if (type == "guid") {
        color = QColor(78, 201, 176);
    } else if (is_numeric_column_type(type)) {
        color = QColor(181, 206, 168);
    } else {
        return {};
    }

    const auto base = qApp == nullptr
        ? QColor(Qt::white)
        : qApp->palette().color(QPalette::Base);
    color.setAlpha(base.lightness() < 128 ? 24 : 18);
    return QBrush(color);
}

QVariant thumbnail_icon(const EntrySummary& summary) {
    if (summary.thumbnail_bytes.empty()) {
        return {};
    }

    QPixmap pixmap;
    if (!pixmap.loadFromData(summary.thumbnail_bytes.data(), static_cast<uint>(summary.thumbnail_bytes.size()))) {
        return {};
    }
    return QIcon(pixmap.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QVariant sort_value(
    bool folder,
    const std::string& name,
    const EntrySummary& summary,
    int column
) {
    if (!folder && summary.source_format == "MUX_PREVIEW") {
        return column == 0 ? QStringLiteral("00mux preview") : QStringLiteral("00");
    }

    switch (column) {
    case 0:
        if (!folder && summary.source_format == "AWB") {
            return QVariant::fromValue(leading_number(summary.name));
        }
        if (is_aix_summary(summary)) {
            return indexed_sort_key(folder, summary);
        }
        return QString(folder ? QLatin1Char('0') : QLatin1Char('1')) +
            folded_display_string(folder ? name : summary.name);
    case 1:
        return folder ? QStringLiteral("0folder") : QStringLiteral("1") + folded_display_string(summary.type);
    case 2:
        return folder ? QVariant::fromValue<qulonglong>(0) : QVariant::fromValue(leading_number(summary.size));
    case 3:
        return folder ? QVariant::fromValue<qulonglong>(0) : QVariant::fromValue(leading_number(summary.offset));
    case 4:
        return folded_display_string(summary.detail);
    default:
        return {};
    }
}

} // namespace

EntryTableModel::EntryTableModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>()) {}

QModelIndex EntryTableModel::index(int row, int column, const QModelIndex& parent) const {
    if ((parent.isValid() && parent.column() != 0) || !hasIndex(row, column, parent)) {
        return {};
    }

    const auto* parent_node = node_from_index(parent);
    return createIndex(row, column, parent_node->children[static_cast<size_t>(row)].get());
}

QModelIndex EntryTableModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }

    const auto* node = node_from_index(child);
    const auto* parent_node = node->parent;
    if (parent_node == nullptr || parent_node == m_root.get()) {
        return {};
    }

    return createIndex(row_of(parent_node), 0, const_cast<Node*>(parent_node));
}

int EntryTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() && parent.column() != 0) {
        return 0;
    }
    return static_cast<int>(node_from_index(parent)->children.size());
}

int EntryTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid() && parent.column() != 0) {
        return 0;
    }
    return table_mode() ? static_cast<int>(m_columns.size()) : 5;
}

QVariant EntryTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const auto* node = node_from_index(index);
    const auto& summary = node->value();
    if (table_mode()) {
        if (role == Qt::ToolTipRole) {
            const auto column = static_cast<size_t>(index.column());
            if (column < summary.cells.size()) {
                return display_string(summary.cells[column]);
            }
            return display_string(summary.name);
        }
        if (role == FullPathRole) {
            return node->search_text;
        }
        if (role == SortRole) {
            const auto column = static_cast<size_t>(index.column());
            const auto text = column < summary.cells.size() ? summary.cells[column] : std::string{};
            if (column < m_column_types.size() && is_numeric_column_type(m_column_types[column])) {
                return QVariant::fromValue(leading_number(text));
            }
            return folded_display_string(text);
        }
        if (role == Qt::BackgroundRole && index.column() > 0 && index.column() < static_cast<int>(m_column_types.size())) {
            return table_column_tint(m_column_types[static_cast<size_t>(index.column())]);
        }
        if (role == Qt::DecorationRole && index.column() == 0) {
            if (auto thumbnail = thumbnail_icon(summary); thumbnail.isValid()) {
                return thumbnail;
            }
            return make_entry_icon(
                display_string(summary.type),
                display_string(summary.name),
                false
            );
        }
        if (role != Qt::DisplayRole) {
            return {};
        }

        const auto column = static_cast<size_t>(index.column());
        if (column < summary.cells.size()) {
            return display_string(summary.cells[column]);
        }
        return {};
    }

    if (m_flat_mode) {
        if (role == Qt::ToolTipRole) {
            switch (index.column()) {
            case 0: return display_string(node->folder ? node->name : summary.name);
            case 1: return node->folder ? QStringLiteral("folder") : display_string(summary.type);
            case 2: return node->folder ? QVariant{} : display_string(summary.size);
            case 3: return node->folder ? QVariant{} : display_string(summary.offset);
            case 4: return display_string(summary.detail);
            default: return {};
            }
        }
        if (role == IsFolderRole) {
            return node->folder;
        }
        if (role == FolderPathRole && node->folder) {
            return display_string(summary.name);
        }
        if (role == FullPathRole) {
            return node->search_text;
        }
        if (role == Qt::DecorationRole && index.column() == 0) {
            if (!node->folder) {
                if (auto thumbnail = thumbnail_icon(summary); thumbnail.isValid()) {
                    return thumbnail;
                }
            }
            return make_entry_icon(
                display_string(summary.type),
                display_string(summary.name),
                node->folder
            );
        }
        if (role == SortRole) {
            return sort_value(node->folder, node->name, summary, index.column());
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case 0:
            return display_string(node->folder ? node->name + "/" : node->name);
        case 1:
            return node->folder ? QStringLiteral("folder") : display_string(summary.type);
        case 2:
            return node->folder ? QVariant{} : display_string(summary.size);
        case 3:
            return node->folder ? QVariant{} : display_string(summary.offset);
        case 4:
            return display_string(summary.detail);
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case 0: return display_string(node->folder ? node->name : summary.name);
        case 1: return node->folder ? QStringLiteral("folder") : display_string(summary.type);
        case 2: return node->folder ? QVariant{} : display_string(summary.size);
        case 3: return node->folder ? QVariant{} : display_string(summary.offset);
        case 4: return display_string(summary.detail);
        default: return {};
        }
    }
    if (role == FullPathRole) {
        return node->search_text;
    }
    if (role == Qt::DecorationRole && index.column() == 0) {
        if (!node->folder) {
            if (auto thumbnail = thumbnail_icon(summary); thumbnail.isValid()) {
                return thumbnail;
            }
        }
        return make_entry_icon(
            display_string(summary.type),
            display_string(node->folder ? node->name : summary.name),
            node->folder
        );
    }
    if (role == SortRole) {
        return sort_value(node->folder, node->name, summary, index.column());
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case 0:
        return display_string(node->folder ? node->name + "/" : node->name);
    case 1:
        return node->folder ? QStringLiteral("folder") : display_string(summary.type);
    case 2:
        return node->folder ? QVariant{} : display_string(summary.size);
    case 3:
        return node->folder ? QVariant{} : display_string(summary.offset);
    case 4:
        return display_string(summary.detail);
    default:
        return {};
    }
}

QVariant EntryTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) {
        return {};
    }

    if (table_mode()) {
        if (section < 0 || section >= static_cast<int>(m_columns.size())) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return display_string(m_columns[static_cast<size_t>(section)]);
        }
        if (role == Qt::ToolTipRole && section < static_cast<int>(m_column_types.size())) {
            const auto& type = m_column_types[static_cast<size_t>(section)];
            return type.empty() ? QVariant{} : display_string(type);
        }
        if (role == Qt::BackgroundRole && section > 0 && section < static_cast<int>(m_column_types.size())) {
            return table_column_tint(m_column_types[static_cast<size_t>(section)]);
        }
        return {};
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0:
        return QStringLiteral("Name");
    case 1:
        return QStringLiteral("Type");
    case 2:
        return QStringLiteral("Size");
    case 3:
        return QStringLiteral("Offset");
    case 4:
        return QStringLiteral("Detail");
    default:
        return {};
    }
}

void EntryTableModel::set_entries(
    std::vector<EntrySummary> entries,
    std::vector<std::string> columns,
    std::vector<std::string> column_types
) {
    set_entries_view(std::move(entries), std::move(columns), std::move(column_types), m_flat_mode, m_flat_path);
}

void EntryTableModel::set_entries_view(
    std::vector<EntrySummary> entries,
    std::vector<std::string> columns,
    std::vector<std::string> column_types,
    bool flat_mode,
    std::string flat_path
) {
    beginResetModel();
    m_entries = std::move(entries);
    m_columns = std::move(columns);
    m_column_types = std::move(column_types);
    if (m_column_types.size() < m_columns.size()) {
        m_column_types.resize(m_columns.size());
    }
    m_flat_mode = flat_mode && !table_mode();
    m_flat_path = normalize_entry_path(std::move(flat_path));
    m_scratch_summary = {};
    rebuild();
    endResetModel();
}

void EntryTableModel::rebuild() {
    m_root = std::make_unique<Node>();
    if (table_mode()) {
        for (const auto& entry : m_entries) {
            add_table_row(entry);
        }
    } else if (m_flat_mode) {
        for (const auto& entry : m_entries) {
            add_flat_path_row(entry);
        }
    } else {
        for (const auto& entry : m_entries) {
            add_entry(entry);
        }
        compress_folder_paths(*m_root);
    }
    update_folder_details(*m_root);
    update_search_text(*m_root);
}

void EntryTableModel::clear() {
    beginResetModel();
    m_entries.clear();
    m_entries.shrink_to_fit();
    m_columns.clear();
    m_columns.shrink_to_fit();
    m_column_types.clear();
    m_column_types.shrink_to_fit();
    m_flat_path.clear();
    m_flat_path.shrink_to_fit();
    m_scratch_summary = {};
    m_root = std::make_unique<Node>();
    endResetModel();
}

bool EntryTableModel::has_custom_columns() const {
    return table_mode();
}

void EntryTableModel::set_flat_mode(bool enabled) {
    if (m_flat_mode == enabled) {
        return;
    }
    beginResetModel();
    m_flat_mode = enabled;
    rebuild();
    endResetModel();
}

bool EntryTableModel::flat_mode() const {
    return m_flat_mode;
}

void EntryTableModel::set_flat_path(std::string path) {
    path = normalize_entry_path(std::move(path));
    if (m_flat_path == path) {
        return;
    }
    beginResetModel();
    m_flat_path = std::move(path);
    rebuild();
    endResetModel();
}

const std::string& EntryTableModel::flat_path() const {
    return m_flat_path;
}

bool EntryTableModel::flat_can_go_up() const {
    return !m_flat_path.empty();
}

std::string EntryTableModel::flat_parent_path() const {
    const auto path = normalize_entry_path(m_flat_path);
    const auto separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return {};
    }
    return path.substr(0, separator);
}

const EntrySummary* EntryTableModel::summary_at(const QModelIndex& index) const {
    if (!index.isValid()) {
        return nullptr;
    }
    const auto* node = node_from_index(index);
    const auto& summary = node->value();
    if (table_mode()) {
        const auto column = static_cast<size_t>(index.column());
        if (
            column < summary.cell_source_indices.size() &&
            summary.cell_source_indices[column] != std::numeric_limits<uint32_t>::max()
        ) {
            m_scratch_summary = summary;
            const auto header = column < m_columns.size() ? m_columns[column] : std::string("cell");
            const auto type = column < m_column_types.size() ? m_column_types[column] : std::string{};
            m_scratch_summary.name = summary.name + "/" + header;
            m_scratch_summary.type = type.empty() ? header : type;
            m_scratch_summary.detail = column < summary.cells.size() ? summary.cells[column] : std::string{};
            m_scratch_summary.source_index = summary.cell_source_indices[column];
            m_scratch_summary.has_source = true;
            if (summary.has_nested_source) {
                m_scratch_summary.nested_source_index = summary.cell_source_indices[column];
                m_scratch_summary.has_nested_source = true;
            }
            return &m_scratch_summary;
        }
        return &summary;
    }
    return node->folder ? nullptr : &summary;
}

EntryTableModel::Node* EntryTableModel::node_from_index(const QModelIndex& index) const {
    if (!index.isValid()) {
        return m_root.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

int EntryTableModel::row_of(const Node* node) const {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }

    const auto& siblings = node->parent->children;
    const auto it = std::ranges::find_if(siblings, [node](const std::unique_ptr<Node>& sibling) {
        return sibling.get() == node;
    });
    return it == siblings.end() ? 0 : static_cast<int>(std::distance(siblings.begin(), it));
}

EntryTableModel::Node* EntryTableModel::find_or_add_folder(Node& parent, std::string name) {
    if (auto it = parent.child_lookup.find(name); it != parent.child_lookup.end()) {
        return it->second;
    }

    auto child = std::make_unique<Node>();
    child->name = std::move(name);
    child->summary.name = child->name;
    child->summary.type = "folder";
    child->folder = true;
    child->parent = &parent;
    auto* raw = child.get();
    parent.child_lookup.emplace(raw->name, raw);
    parent.children.push_back(std::move(child));
    return raw;
}

void EntryTableModel::add_entry(const EntrySummary& entry) {
    auto parts = split_entry_path(entry.name);
    auto* parent = m_root.get();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        parent = find_or_add_folder(*parent, std::move(parts[i]));
    }

    auto leaf = std::make_unique<Node>();
    leaf->name = std::move(parts.back());
    leaf->source_summary = &entry;
    leaf->parent = parent;
    parent->children.push_back(std::move(leaf));
}

void EntryTableModel::add_table_row(const EntrySummary& entry) {
    auto row = std::make_unique<Node>();
    row->name = entry.name;
    row->source_summary = &entry;
    row->parent = m_root.get();
    m_root->children.push_back(std::move(row));
}

void EntryTableModel::add_flat_path_row(const EntrySummary& entry) {
    const auto normalized = normalize_entry_path(entry.name);
    const auto prefix = m_flat_path.empty() ? std::string{} : m_flat_path + "/";
    if (!prefix.empty() && !normalized.starts_with(prefix)) {
        return;
    }

    const auto remainder = prefix.empty() ? normalized : normalized.substr(prefix.size());
    if (remainder.empty()) {
        return;
    }

    if (const auto separator = remainder.find('/'); separator != std::string::npos) {
        const auto folder_name = remainder.substr(0, separator);
        auto* folder = find_or_add_folder(*m_root, folder_name);
        folder->summary.name = prefix + folder_name;
        return;
    }

    auto row = std::make_unique<Node>();
    row->name = remainder;
    row->source_summary = &entry;
    row->parent = m_root.get();
    m_root->children.push_back(std::move(row));
}

void EntryTableModel::compress_folder_paths(Node& node) {
    for (const auto& child : node.children) {
        compress_folder_paths(*child);
    }

    for (const auto& child : node.children) {
        auto* current = child.get();
        if (!current->folder) {
            continue;
        }

        while (current->children.size() == 1 && current->children.front()->folder) {
            auto next = std::move(current->children.front());
            current->child_lookup.clear();
            current->children.clear();
            current->name += "/" + next->name;
            current->summary.name = current->name;
            current->children = std::move(next->children);
            current->child_lookup.clear();
            for (const auto& grandchild : current->children) {
                grandchild->parent = current;
                current->child_lookup.emplace(grandchild->name, grandchild.get());
            }
        }
    }
}

void EntryTableModel::update_folder_details(Node& node) {
    for (const auto& child : node.children) {
        update_folder_details(*child);
    }

    if (node.folder) {
        const auto child_count = node.children.size();
        node.summary.detail = std::to_string(child_count) + (child_count == 1 ? " item" : " items");
        for (const auto& child : node.children) {
            const auto& child_summary = child->value();
            if (!child_summary.has_source) {
                continue;
            }
            if (!node.summary.has_source || child_summary.source_index < node.summary.source_index) {
                node.summary.source_path = child_summary.source_path;
                node.summary.source_format = child_summary.source_format;
                node.summary.source_index = child_summary.source_index;
                node.summary.has_source = true;
            }
        }
    }
}

void EntryTableModel::update_search_text(Node& node) {
    const auto& summary = node.value();
    QStringList parts;
    if (table_mode()) {
        parts.reserve(static_cast<int>(summary.cells.size() + summary.inspector_entries.size() + 1));
        parts.push_back(display_string(summary.name));
        for (const auto& cell : summary.cells) {
            parts.push_back(display_string(cell));
        }
        for (const auto& field : summary.inspector_entries) {
            parts.push_back(display_string(field.name));
            parts.push_back(display_string(field.type));
            parts.push_back(display_string(field.detail));
            for (const auto& cell : field.cells) {
                parts.push_back(display_string(cell));
            }
        }
    } else if (m_flat_mode) {
        parts.reserve(static_cast<int>(summary.cells.size() + summary.inspector_entries.size() + 5));
        parts.push_back(display_string(summary.name));
        parts.push_back(display_string(summary.type));
        parts.push_back(display_string(summary.size));
        parts.push_back(display_string(summary.offset));
        parts.push_back(display_string(summary.detail));
        for (const auto& cell : summary.cells) {
            parts.push_back(display_string(cell));
        }
        for (const auto& field : summary.inspector_entries) {
            parts.push_back(display_string(field.name));
            parts.push_back(display_string(field.type));
            parts.push_back(display_string(field.detail));
        }
    } else {
        parts.push_back(display_string(summary.name));
    }
    node.search_text = parts.join(QLatin1Char(' '));
    for (const auto& child : node.children) {
        update_search_text(*child);
    }
}

bool EntryTableModel::table_mode() const {
    return !m_columns.empty();
}

} // namespace cristudio
