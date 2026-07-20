#pragma once

#include "modules/transform_detail.hpp"

#include <QAbstractTableModel>

#include <cstddef>
#include <vector>

namespace cristudio {

class TransformDetailModel final : public QAbstractTableModel {
public:
    explicit TransformDetailModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole
    ) const override;

    void set_rows(std::vector<modules::TransformDetailRow> rows);
    [[nodiscard]] const modules::TransformDetailRow* detail_at(int row) const noexcept;
    [[nodiscard]] int find_row(int payload_kind, int index, int layer = -1) const noexcept;

private:
    std::vector<modules::TransformDetailRow> m_rows;
};

} // namespace cristudio
