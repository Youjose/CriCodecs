#pragma once

#include <QString>

namespace cristudio::modules {

struct TransformDetailRow {
    QString field;
    QString value;
    int payload_kind = 0;
    int index = -1;
    int layer = -1;
};

} // namespace cristudio::modules
