#include "editor/transform_detail_model.hpp"

#include <QVariant>

#include <utility>

namespace cristudio {

TransformDetailModel::TransformDetailModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int TransformDetailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int TransformDetailModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 2;
}

QVariant TransformDetailModel::data(const QModelIndex& index, int role) const {
    const auto* detail = detail_at(index.row());
    if (!index.isValid() || detail == nullptr || index.column() < 0 || index.column() >= 2) {
        return {};
    }

    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        return index.column() == 0 ? detail->field : detail->value;
    }
    if (role == Qt::UserRole) {
        return detail->payload_kind;
    }
    if (role == Qt::UserRole + 1) {
        return detail->index;
    }
    if (role == Qt::UserRole + 2) {
        return detail->layer;
    }
    return {};
}

QVariant TransformDetailModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role
) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    if (section == 0) {
        return QStringLiteral("Field");
    }
    if (section == 1) {
        return QStringLiteral("Value");
    }
    return {};
}

void TransformDetailModel::set_rows(std::vector<modules::TransformDetailRow> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

const modules::TransformDetailRow* TransformDetailModel::detail_at(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    return &m_rows[static_cast<size_t>(row)];
}

int TransformDetailModel::find_row(int payload_kind, int index, int layer) const noexcept {
    for (size_t row = 0; row < m_rows.size(); ++row) {
        const auto& detail = m_rows[row];
        if (detail.payload_kind == payload_kind && detail.index == index &&
            (layer < 0 || detail.layer == layer)) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

} // namespace cristudio
