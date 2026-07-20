#pragma once

#include "modules/transform_detail.hpp"

#include "sfd_container.hpp"

#include <QString>

#include <expected>
#include <string>
#include <vector>

namespace cristudio::modules::sfd {

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    const cricodecs::sfd::SfdContainer& sfd
);

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::sfd::SfdContainer& sfd);

[[nodiscard]] QString header_summary_preview(const cricodecs::sfd::SfdHeaderSummary& summary);
[[nodiscard]] QString element_record_preview(const cricodecs::sfd::SfdElementRecord& record);
[[nodiscard]] QString stream_detail_preview(const cricodecs::sfd::SfdStream& stream);
[[nodiscard]] std::expected<QString, QString> payload_preview(
    const cricodecs::sfd::SfdContainer& sfd,
    int payload_kind,
    int index
);

} // namespace cristudio::modules::sfd
