#pragma once

#include "acx_container.hpp"

class QTableWidget;

namespace cristudio::modules::acx {

void populate_editor_archive_table(QTableWidget* table, const cricodecs::acx::AcxContainer& acx);

} // namespace cristudio::modules::acx
