#include "file_list_model.hpp"

#include "format_icon.hpp"
#include "path_text.hpp"

#include <algorithm>
#include <utility>

namespace cristudio {
namespace {

QString build_search_text(const LoadedDocument& document, const QString& canonical_path) {
    QString text;
    text.reserve(static_cast<qsizetype>(document.entries.size() * 48 + 256));
    text += utf8_to_qstring(document.display_name);
    text += QLatin1Char('\n');
    text += utf8_to_qstring(document.format);
    text += QLatin1Char('\n');
    text += path_to_qstring(document.path);
    text += QLatin1Char('\n');
    text += canonical_path;
    for (const auto& entry : document.entries) {
        text += QLatin1Char('\n');
        text += utf8_to_qstring(entry.name);
        text += QLatin1Char('\n');
        text += utf8_to_qstring(entry.type);
        if (!entry.detail.empty()) {
            text += QLatin1Char('\n');
            text += utf8_to_qstring(entry.detail);
        }
    }
    return text;
}

QString filter_format(const LoadedDocument& document) {
    const auto format = utf8_to_qstring(document.format);
    if (format.contains(QStringLiteral("ADX"), Qt::CaseInsensitive) ||
        format.contains(QStringLiteral("AHX"), Qt::CaseInsensitive)) {
        return QStringLiteral("ADX / AHX audio");
    }
    return format;
}

} // namespace

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int FileListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_items.size())) {
        return {};
    }

    const auto& item = m_items[static_cast<size_t>(index.row())];
    const auto& doc = item.document;
    switch (role) {
    case Qt::DisplayRole:
        return utf8_to_qstring(doc.display_name);
    case Qt::ToolTipRole:
        return utf8_to_qstring(doc.format) + QLatin1Char('\n') + path_to_qstring(doc.path);
    case Qt::DecorationRole:
        return make_document_icon(
            utf8_to_qstring(doc.format),
            utf8_to_qstring(doc.display_name)
        );
    case FormatRole:
        return utf8_to_qstring(doc.format);
    case PathRole:
        return path_to_qstring(doc.path);
    case SearchRole:
        return item.search_text;
    case NameSortRole:
        return utf8_to_qstring(doc.display_name).toCaseFolded();
    case SizeSortRole:
        return QVariant::fromValue<qulonglong>(doc.file_size);
    case FilterFormatRole:
        return filter_format(doc);
    default:
        return {};
    }
}

const LoadedDocument* FileListModel::document_at(int row) const {
    if (row < 0 || row >= static_cast<int>(m_items.size())) {
        return nullptr;
    }
    return &m_items[static_cast<size_t>(row)].document;
}

int FileListModel::index_of_path(const QString& canonical_path) const {
    const auto found = m_row_by_path.constFind(QStringView(canonical_path));
    return found == m_row_by_path.cend() ? -1 : found.value();
}

QString FileListModel::canonical_path_at(int row) const {
    if (row < 0 || row >= static_cast<int>(m_items.size())) {
        return {};
    }
    return m_items[static_cast<size_t>(row)].canonical_path;
}

void FileListModel::reserve(int count) {
    if (count <= 0) {
        return;
    }
    m_items.reserve(static_cast<size_t>(count));
    m_row_by_path.reserve(count);
}

void FileListModel::add_document(LoadedDocument document, const QString& canonical_path) {
    const auto row = static_cast<int>(m_items.size());
    auto search_text = build_search_text(document, canonical_path);
    beginInsertRows({}, row, row);
    m_items.push_back({std::move(document), canonical_path, std::move(search_text)});
    m_row_by_path.emplace(QStringView(m_items.back().canonical_path), row);
    endInsertRows();
}

void FileListModel::replace_document(int row, LoadedDocument document) {
    if (row < 0 || row >= static_cast<int>(m_items.size())) {
        return;
    }

    auto& item = m_items[static_cast<size_t>(row)];
    item.document = std::move(document);
    item.search_text = build_search_text(item.document, item.canonical_path);
    const auto model_index = index(row, 0);
    emit dataChanged(model_index, model_index, {});
}

void FileListModel::remove_rows(std::vector<int> rows) {
    std::ranges::sort(rows);
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    const auto row_count = static_cast<int>(m_items.size());
    std::erase_if(rows, [row_count](int row) { return row < 0 || row >= row_count; });
    if (rows.empty()) {
        return;
    }

    const bool contiguous = rows.back() - rows.front() + 1 == static_cast<int>(rows.size());
    if (contiguous) {
        const int first = rows.front();
        const int last = rows.back();
        beginRemoveRows({}, first, last);
        m_items.erase(m_items.begin() + first, m_items.begin() + last + 1);
        rebuild_path_index();
        endRemoveRows();
        return;
    }

    beginResetModel();
    size_t row_index = 0;
    size_t removed_index = 0;
    std::erase_if(m_items, [&](const Item&) {
        const bool remove = removed_index < rows.size() &&
            row_index == static_cast<size_t>(rows[removed_index]);
        ++row_index;
        removed_index += static_cast<size_t>(remove);
        return remove;
    });
    rebuild_path_index();
    endResetModel();
}

void FileListModel::clear() {
    if (m_items.empty()) {
        return;
    }
    beginResetModel();
    m_row_by_path.clear();
    m_row_by_path.squeeze();
    m_items.clear();
    m_items.shrink_to_fit();
    endResetModel();
}

void FileListModel::rebuild_path_index() {
    m_row_by_path.clear();
    m_row_by_path.reserve(static_cast<qsizetype>(m_items.size()));
    for (size_t row = 0; row < m_items.size(); ++row) {
        m_row_by_path.emplace(QStringView(m_items[row].canonical_path), static_cast<int>(row));
    }
}

} // namespace cristudio
