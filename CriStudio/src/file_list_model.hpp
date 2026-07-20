#pragma once

#include "document_loader.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QStringView>

#include <vector>

namespace cristudio {

class FileListModel final : public QAbstractListModel {
public:
    enum Role : int {
        FormatRole = Qt::UserRole + 1,
        PathRole,
        SearchRole,
        NameSortRole,
        SizeSortRole,
        FilterFormatRole
    };

    explicit FileListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    [[nodiscard]] const LoadedDocument* document_at(int row) const;
    [[nodiscard]] int index_of_path(const QString& canonical_path) const;
    [[nodiscard]] QString canonical_path_at(int row) const;

    void reserve(int count);
    void add_document(LoadedDocument document, const QString& canonical_path);
    void replace_document(int row, LoadedDocument document);
    void remove_rows(std::vector<int> rows);
    void clear();

private:
    struct Item {
        LoadedDocument document;
        QString canonical_path;
        QString search_text;
    };

    void rebuild_path_index();

    std::vector<Item> m_items;
    // Views avoid duplicating key objects; m_items owns the immutable strings.
    QHash<QStringView, int> m_row_by_path;
};

} // namespace cristudio
