#pragma once
#include "hca_format.hpp"
#include "hca_frame.hpp"
#include "hca_tables.hpp"

namespace cricodecs::hca::packing {

[[nodiscard]] inline uint8_t scalefactor_count_for_header(const HcaHeader& info, const HcaChannel& channel) noexcept {
    if (detail::uses_v3_frame_layout(info.file.version) && channel.type != ChannelType::StereoSecondary) {
        return static_cast<uint8_t>(channel.coded_count + info.codec.hfr_group_count);
    }
    return channel.coded_count;
}

void pack_frame(HcaFrame& frame, uint8_t* buffer);

} // namespace cricodecs::hca::packing
