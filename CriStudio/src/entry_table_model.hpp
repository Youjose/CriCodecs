#pragma once

#include "document/document_types.hpp"

#include <QAbstractItemModel>
#include <QString>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cristudio {

class EntryTableModel final : public QAbstractItemModel {
public:
    enum Role : int {
        FullPathRole = Qt::UserRole + 1,
        SortRole,
        IsFolderRole,
        FolderPathRole
    };

    explicit EntryTableModel(QObject* parent = nullptr);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void set_entries(
        std::vector<EntrySummary> entries,
        std::vector<std::string> columns = {},
        std::vector<std::string> column_types = {}
    );
    void set_entries_view(
        std::vector<EntrySummary> entries,
        std::vector<std::string> columns,
        std::vector<std::string> column_types,
        bool flat_mode,
        std::string flat_path
    );
    void clear();
    [[nodiscard]] const EntrySummary* summary_at(const QModelIndex& index) const;
    [[nodiscard]] bool has_custom_columns() const;
    void set_flat_mode(bool enabled);
    [[nodiscard]] bool flat_mode() const;
    void set_flat_path(std::string path);
    [[nodiscard]] const std::string& flat_path() const;
    [[nodiscard]] bool flat_can_go_up() const;
    [[nodiscard]] std::string flat_parent_path() const;

private:
    struct Node {
        std::string name;
        EntrySummary summary;
        const EntrySummary* source_summary = nullptr;
        QString search_text;
        Node* parent = nullptr;
        bool folder = false;
        std::vector<std::unique_ptr<Node>> children;
        std::unordered_map<std::string, Node*> child_lookup;

        [[nodiscard]] const EntrySummary& value() const noexcept {
            return source_summary == nullptr ? summary : *source_summary;
        }
    };

    [[nodiscard]] Node* node_from_index(const QModelIndex& index) const;
    [[nodiscard]] int row_of(const Node* node) const;
    [[nodiscard]] Node* find_or_add_folder(Node& parent, std::string name);
    void rebuild();
    void add_entry(const EntrySummary& entry);
    void add_table_row(const EntrySummary& entry);
    void add_flat_path_row(const EntrySummary& entry);
    void compress_folder_paths(Node& node);
    void update_folder_details(Node& node);
    void update_search_text(Node& node);
    [[nodiscard]] bool table_mode() const;

    std::unique_ptr<Node> m_root;
    std::vector<EntrySummary> m_entries;
    std::vector<std::string> m_columns;
    std::vector<std::string> m_column_types;
    std::string m_flat_path;
    mutable EntrySummary m_scratch_summary;
    bool m_flat_mode = false;
};

} // namespace cristudio
