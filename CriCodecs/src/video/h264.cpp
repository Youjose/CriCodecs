/**
 * @file h264.cpp
 * @brief H.264 Annex B structural parser for USM frame packaging.
 */

#include "h264.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <ranges>

namespace cricodecs::video {

namespace {

constexpr size_t npos = std::numeric_limits<size_t>::max();

struct NalUnit {
    size_t offset = 0;
    size_t payload_offset = 0;
    size_t end = 0;
    uint8_t type = 0;
};

struct AnnexBStartCode {
    size_t offset = npos;
    size_t size = 0;
};

[[nodiscard]] AnnexBStartCode find_annex_b_start_code(std::span<const uint8_t> bytes, size_t offset = 0) noexcept {
    const auto* data = bytes.data();
    const size_t size = bytes.size();
    while (offset + 3u <= size) {
        const auto* zero = static_cast<const uint8_t*>(std::memchr(data + offset, 0, size - offset - 2u));
        if (zero == nullptr) {
            return {};
        }

        offset = static_cast<size_t>(zero - data);
        if (offset + 4u <= size && data[offset + 1u] == 0 && data[offset + 2u] == 0 && data[offset + 3u] == 1) {
            return AnnexBStartCode{.offset = offset, .size = 4};
        }
        if (data[offset + 1u] == 0 && data[offset + 2u] == 1) {
            return AnnexBStartCode{.offset = offset, .size = 3};
        }
        ++offset;
    }
    return {};
}

std::expected<uint32_t, std::string> read_bits(io::bit_reader& br, int bits) {
    auto value = br.read_checked(bits);
    if (!value) {
        return std::unexpected("H.264 bitstream parse failed: " + std::string(value.error()));
    }
    return *value;
}

std::expected<bool, std::string> read_bit(io::bit_reader& br) {
    auto value = read_bits(br, 1);
    if (!value) {
        return std::unexpected(value.error());
    }
    return *value != 0;
}

std::expected<uint32_t, std::string> read_ue(io::bit_reader& br) {
    uint32_t leading_zero_bits = 0;
    bool found_stop_bit = false;
    while (br.remaining() != 0) {
        auto bit = read_bit(br);
        if (!bit) {
            return std::unexpected(bit.error());
        }
        if (*bit) {
            found_stop_bit = true;
            break;
        }
        ++leading_zero_bits;
        if (leading_zero_bits > 31u) {
            return std::unexpected("H.264 bitstream parse failed: Exp-Golomb code is too wide");
        }
    }

    if (!found_stop_bit) {
        return std::unexpected("H.264 bitstream parse failed: truncated Exp-Golomb code");
    }
    if (leading_zero_bits == 0) {
        return 0u;
    }

    auto suffix = read_bits(br, static_cast<int>(leading_zero_bits));
    if (!suffix) {
        return std::unexpected(suffix.error());
    }
    return ((1u << leading_zero_bits) - 1u) + *suffix;
}

std::expected<int32_t, std::string> read_se(io::bit_reader& br) {
    auto code_num = read_ue(br);
    if (!code_num) {
        return std::unexpected(code_num.error());
    }
    const int32_t value = static_cast<int32_t>((*code_num + 1u) / 2u);
    return (*code_num & 1u) != 0 ? value : -value;
}

[[nodiscard]] bool is_vcl_nal(uint8_t nal_type) noexcept {
    return nal_type >= 1u && nal_type <= 5u;
}

[[nodiscard]] std::vector<NalUnit> find_annex_b_nals(std::span<const uint8_t> bytes) {
    std::vector<NalUnit> nals;
    for (auto start_code = find_annex_b_start_code(bytes);
        start_code.offset != npos;
        start_code = find_annex_b_start_code(bytes, start_code.offset + start_code.size)) {

        const size_t payload_offset = start_code.offset + start_code.size;
        if (payload_offset >= bytes.size()) {
            break;
        }
        nals.push_back(NalUnit{
            .offset = start_code.offset,
            .payload_offset = payload_offset,
            .end = bytes.size(),
            .type = static_cast<uint8_t>(bytes[payload_offset] & 0x1Fu),
        });
    }

    for (size_t index = 0; index + 1u < nals.size(); ++index) {
        nals[index].end = nals[index + 1u].offset;
    }
    return nals;
}

[[nodiscard]] std::vector<uint8_t> make_rbsp(std::span<const uint8_t> nal_payload) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal_payload.size());
    uint8_t zero_count = 0;
    for (const uint8_t byte : nal_payload) {
        if (zero_count >= 2u && byte == 0x03u) {
            zero_count = 0;
            continue;
        }
        rbsp.push_back(byte);
        zero_count = byte == 0 ? static_cast<uint8_t>(zero_count + 1u) : 0u;
    }
    return rbsp;
}

std::expected<void, std::string> skip_scaling_list(io::bit_reader& br, uint32_t size) {
    int32_t last_scale = 8;
    int32_t next_scale = 8;
    for (uint32_t index = 0; index < size; ++index) {
        if (next_scale != 0) {
            auto delta_scale = read_se(br);
            if (!delta_scale) {
                return std::unexpected(delta_scale.error());
            }
            next_scale = (last_scale + *delta_scale + 256) % 256;
        }
        last_scale = next_scale == 0 ? last_scale : next_scale;
    }
    return {};
}

std::expected<void, std::string> skip_vui_prefix_before_timing(io::bit_reader& br) {
    auto aspect_ratio_info_present_flag = read_bit(br);
    if (!aspect_ratio_info_present_flag) {
        return std::unexpected(aspect_ratio_info_present_flag.error());
    }
    if (*aspect_ratio_info_present_flag) {
        auto aspect_ratio_idc = read_bits(br, 8);
        if (!aspect_ratio_idc) {
            return std::unexpected(aspect_ratio_idc.error());
        }
        if (*aspect_ratio_idc == 255u) {
            if (auto sar_width = read_bits(br, 16); !sar_width) {
                return std::unexpected(sar_width.error());
            }
            if (auto sar_height = read_bits(br, 16); !sar_height) {
                return std::unexpected(sar_height.error());
            }
        }
    }

    auto overscan_info_present_flag = read_bit(br);
    if (!overscan_info_present_flag) {
        return std::unexpected(overscan_info_present_flag.error());
    }
    if (*overscan_info_present_flag) {
        if (auto overscan_appropriate_flag = read_bit(br); !overscan_appropriate_flag) {
            return std::unexpected(overscan_appropriate_flag.error());
        }
    }

    auto video_signal_type_present_flag = read_bit(br);
    if (!video_signal_type_present_flag) {
        return std::unexpected(video_signal_type_present_flag.error());
    }
    if (*video_signal_type_present_flag) {
        if (auto video_format = read_bits(br, 3); !video_format) {
            return std::unexpected(video_format.error());
        }
        if (auto video_full_range_flag = read_bit(br); !video_full_range_flag) {
            return std::unexpected(video_full_range_flag.error());
        }
        auto colour_description_present_flag = read_bit(br);
        if (!colour_description_present_flag) {
            return std::unexpected(colour_description_present_flag.error());
        }
        if (*colour_description_present_flag) {
            if (auto colour_primaries = read_bits(br, 8); !colour_primaries) {
                return std::unexpected(colour_primaries.error());
            }
            if (auto transfer_characteristics = read_bits(br, 8); !transfer_characteristics) {
                return std::unexpected(transfer_characteristics.error());
            }
            if (auto matrix_coefficients = read_bits(br, 8); !matrix_coefficients) {
                return std::unexpected(matrix_coefficients.error());
            }
        }
    }

    auto chroma_loc_info_present_flag = read_bit(br);
    if (!chroma_loc_info_present_flag) {
        return std::unexpected(chroma_loc_info_present_flag.error());
    }
    if (*chroma_loc_info_present_flag) {
        if (auto chroma_sample_loc_type_top_field = read_ue(br); !chroma_sample_loc_type_top_field) {
            return std::unexpected(chroma_sample_loc_type_top_field.error());
        }
        if (auto chroma_sample_loc_type_bottom_field = read_ue(br); !chroma_sample_loc_type_bottom_field) {
            return std::unexpected(chroma_sample_loc_type_bottom_field.error());
        }
    }

    return {};
}

} // namespace

std::expected<H264SequenceParameterSet, std::string> parse_h264_sequence_parameter_set(std::span<const uint8_t> bytes) {
    const auto nals = find_annex_b_nals(bytes);
    const auto sps_nal = std::ranges::find_if(nals, [](const NalUnit& nal) {
        return nal.type == 7u;
    });
    if (sps_nal == nals.end() || sps_nal->payload_offset + 1u >= sps_nal->end) {
        return std::unexpected("H.264 video parse failed: SPS NAL not found");
    }

    const auto rbsp = make_rbsp(bytes.subspan(sps_nal->payload_offset + 1u, sps_nal->end - sps_nal->payload_offset - 1u));
    io::bit_reader br(rbsp);

    auto profile_idc = read_bits(br, 8);
    auto constraint_flags = read_bits(br, 8);
    auto level_idc = read_bits(br, 8);
    auto seq_parameter_set_id = read_ue(br);
    if (!profile_idc || !constraint_flags || !level_idc || !seq_parameter_set_id) {
        return std::unexpected("H.264 video parse failed: truncated SPS header");
    }

    uint32_t chroma_format_idc = 1;
    bool separate_colour_plane_flag = false;
    switch (*profile_idc) {
        case 100:
        case 110:
        case 122:
        case 244:
        case 44:
        case 83:
        case 86:
        case 118:
        case 128:
        case 138:
        case 139:
        case 134: {
            auto chroma_format = read_ue(br);
            if (!chroma_format) {
                return std::unexpected(chroma_format.error());
            }
            chroma_format_idc = *chroma_format;
            if (chroma_format_idc == 3u) {
                auto separate_colour_plane = read_bit(br);
                if (!separate_colour_plane) {
                    return std::unexpected(separate_colour_plane.error());
                }
                separate_colour_plane_flag = *separate_colour_plane;
            }
            if (auto bit_depth_luma_minus8 = read_ue(br); !bit_depth_luma_minus8) {
                return std::unexpected(bit_depth_luma_minus8.error());
            }
            if (auto bit_depth_chroma_minus8 = read_ue(br); !bit_depth_chroma_minus8) {
                return std::unexpected(bit_depth_chroma_minus8.error());
            }
            if (auto qpprime_y_zero_transform_bypass_flag = read_bit(br); !qpprime_y_zero_transform_bypass_flag) {
                return std::unexpected(qpprime_y_zero_transform_bypass_flag.error());
            }
            auto seq_scaling_matrix_present_flag = read_bit(br);
            if (!seq_scaling_matrix_present_flag) {
                return std::unexpected(seq_scaling_matrix_present_flag.error());
            }
            if (*seq_scaling_matrix_present_flag) {
                const uint32_t scaling_list_count = chroma_format_idc != 3u ? 8u : 12u;
                for (uint32_t index = 0; index < scaling_list_count; ++index) {
                    auto seq_scaling_list_present_flag = read_bit(br);
                    if (!seq_scaling_list_present_flag) {
                        return std::unexpected(seq_scaling_list_present_flag.error());
                    }
                    if (*seq_scaling_list_present_flag) {
                        if (auto skipped = skip_scaling_list(br, index < 6u ? 16u : 64u); !skipped) {
                            return std::unexpected(skipped.error());
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    if (auto log2_max_frame_num_minus4 = read_ue(br); !log2_max_frame_num_minus4) {
        return std::unexpected(log2_max_frame_num_minus4.error());
    }
    auto pic_order_cnt_type = read_ue(br);
    if (!pic_order_cnt_type) {
        return std::unexpected(pic_order_cnt_type.error());
    }
    if (*pic_order_cnt_type == 0u) {
        if (auto log2_max_pic_order_cnt_lsb_minus4 = read_ue(br); !log2_max_pic_order_cnt_lsb_minus4) {
            return std::unexpected(log2_max_pic_order_cnt_lsb_minus4.error());
        }
    } else if (*pic_order_cnt_type == 1u) {
        if (auto delta_pic_order_always_zero_flag = read_bit(br); !delta_pic_order_always_zero_flag) {
            return std::unexpected(delta_pic_order_always_zero_flag.error());
        }
        if (auto offset_for_non_ref_pic = read_se(br); !offset_for_non_ref_pic) {
            return std::unexpected(offset_for_non_ref_pic.error());
        }
        if (auto offset_for_top_to_bottom_field = read_se(br); !offset_for_top_to_bottom_field) {
            return std::unexpected(offset_for_top_to_bottom_field.error());
        }
        auto num_ref_frames_in_pic_order_cnt_cycle = read_ue(br);
        if (!num_ref_frames_in_pic_order_cnt_cycle) {
            return std::unexpected(num_ref_frames_in_pic_order_cnt_cycle.error());
        }
        for (uint32_t index = 0; index < *num_ref_frames_in_pic_order_cnt_cycle; ++index) {
            if (auto offset_for_ref_frame = read_se(br); !offset_for_ref_frame) {
                return std::unexpected(offset_for_ref_frame.error());
            }
        }
    }

    if (auto max_num_ref_frames = read_ue(br); !max_num_ref_frames) {
        return std::unexpected(max_num_ref_frames.error());
    }
    if (auto gaps_in_frame_num_value_allowed_flag = read_bit(br); !gaps_in_frame_num_value_allowed_flag) {
        return std::unexpected(gaps_in_frame_num_value_allowed_flag.error());
    }
    auto pic_width_in_mbs_minus1 = read_ue(br);
    auto pic_height_in_map_units_minus1 = read_ue(br);
    auto frame_mbs_only_flag = read_bit(br);
    if (!pic_width_in_mbs_minus1 || !pic_height_in_map_units_minus1 || !frame_mbs_only_flag) {
        return std::unexpected("H.264 video parse failed: truncated SPS dimensions");
    }
    if (!*frame_mbs_only_flag) {
        if (auto mb_adaptive_frame_field_flag = read_bit(br); !mb_adaptive_frame_field_flag) {
            return std::unexpected(mb_adaptive_frame_field_flag.error());
        }
    }
    if (auto direct_8x8_inference_flag = read_bit(br); !direct_8x8_inference_flag) {
        return std::unexpected(direct_8x8_inference_flag.error());
    }

    uint32_t frame_crop_left_offset = 0;
    uint32_t frame_crop_right_offset = 0;
    uint32_t frame_crop_top_offset = 0;
    uint32_t frame_crop_bottom_offset = 0;
    auto frame_cropping_flag = read_bit(br);
    if (!frame_cropping_flag) {
        return std::unexpected(frame_cropping_flag.error());
    }
    if (*frame_cropping_flag) {
        auto left = read_ue(br);
        auto right = read_ue(br);
        auto top = read_ue(br);
        auto bottom = read_ue(br);
        if (!left || !right || !top || !bottom) {
            return std::unexpected("H.264 video parse failed: truncated SPS crop rectangle");
        }
        frame_crop_left_offset = *left;
        frame_crop_right_offset = *right;
        frame_crop_top_offset = *top;
        frame_crop_bottom_offset = *bottom;
    }

    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0;
    bool fixed_frame_rate = false;
    auto vui_parameters_present_flag = read_bit(br);
    if (!vui_parameters_present_flag) {
        return std::unexpected(vui_parameters_present_flag.error());
    }
    if (*vui_parameters_present_flag) {
        if (auto skipped = skip_vui_prefix_before_timing(br); !skipped) {
            return std::unexpected(skipped.error());
        }
        auto timing_info_present_flag = read_bit(br);
        if (!timing_info_present_flag) {
            return std::unexpected(timing_info_present_flag.error());
        }
        if (*timing_info_present_flag) {
            auto num_units = read_bits(br, 32);
            auto scale = read_bits(br, 32);
            auto fixed = read_bit(br);
            if (!num_units || !scale || !fixed) {
                return std::unexpected("H.264 video parse failed: truncated SPS timing info");
            }
            num_units_in_tick = *num_units;
            time_scale = *scale;
            fixed_frame_rate = *fixed;
        }
    }

    uint32_t width = (*pic_width_in_mbs_minus1 + 1u) * 16u;
    uint32_t height = (2u - static_cast<uint32_t>(*frame_mbs_only_flag)) * (*pic_height_in_map_units_minus1 + 1u) * 16u;
    const uint32_t chroma_array_type = separate_colour_plane_flag ? 0u : chroma_format_idc;
    uint32_t crop_unit_x = 1;
    uint32_t crop_unit_y = 2u - static_cast<uint32_t>(*frame_mbs_only_flag);
    if (chroma_array_type != 0u) {
        const uint32_t sub_width_c = chroma_array_type == 3u ? 1u : 2u;
        const uint32_t sub_height_c = chroma_array_type == 1u ? 2u : 1u;
        crop_unit_x = sub_width_c;
        crop_unit_y = sub_height_c * (2u - static_cast<uint32_t>(*frame_mbs_only_flag));
    }
    width -= std::min(width, (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x);
    height -= std::min(height, (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y);

    return H264SequenceParameterSet{
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height),
        .profile_idc = static_cast<uint8_t>(*profile_idc),
        .level_idc = static_cast<uint8_t>(*level_idc),
        .num_units_in_tick = num_units_in_tick,
        .time_scale = time_scale,
        .fixed_frame_rate = fixed_frame_rate,
    };
}

std::expected<void, std::string> H264VideoReader::open(const std::filesystem::path& path) {
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("H.264 video load failed: could not open input file: " + path.string());
    }
    return parse_loaded_stream(path.string());
}

std::expected<void, std::string> H264VideoReader::open(std::span<const uint8_t> bytes) {
    if (auto result = m_reader.open(bytes); !result) {
        return std::unexpected("H.264 video load failed: could not open memory buffer");
    }
    return parse_loaded_stream("memory buffer");
}

std::pair<uint32_t, uint32_t> H264VideoReader::frame_rate() const noexcept {
    if (m_sps.num_units_in_tick != 0 && m_sps.time_scale != 0) {
        const uint32_t denominator = m_sps.num_units_in_tick * 2u;
        const uint32_t divisor = std::gcd(m_sps.time_scale, denominator);
        return {m_sps.time_scale / divisor, denominator / divisor};
    }
    return {30000, 1001};
}

std::expected<void, std::string> H264VideoReader::parse_loaded_stream(std::string_view source_name) {
    auto sps = parse_h264_sequence_parameter_set(m_reader.data());
    if (!sps) {
        return std::unexpected("H.264 video load failed for " + std::string(source_name) + ": " + sps.error());
    }

    m_sps = *sps;
    m_frames = split_frames(m_reader.data());
    m_current_frame = 0;
    if (m_frames.empty()) {
        return std::unexpected("H.264 video load failed for " + std::string(source_name) + ": no frames found");
    }
    return {};
}

std::vector<H264VideoReader::FrameRange> H264VideoReader::split_frames(std::span<const uint8_t> bytes) {
    const auto nals = find_annex_b_nals(bytes);
    std::vector<FrameRange> frames;
    if (nals.empty()) {
        return frames;
    }

    const bool has_aud = std::ranges::any_of(nals, [](const NalUnit& nal) {
        return nal.type == 9u;
    });

    size_t current_frame_start = npos;
    bool current_has_vcl = false;
    bool current_keyframe = false;

    auto finish_frame = [&](size_t frame_end) {
        if (current_frame_start != npos && frame_end > current_frame_start && current_has_vcl) {
            frames.push_back(FrameRange{
                .offset = current_frame_start,
                .size = frame_end - current_frame_start,
                .is_keyframe = current_keyframe,
            });
        }
    };

    if (has_aud) {
        for (const auto& nal : nals) {
            if (nal.type == 9u) {
                finish_frame(nal.offset);
                current_frame_start = nal.offset;
                current_has_vcl = false;
                current_keyframe = false;
                continue;
            }
            if (current_frame_start == npos) {
                current_frame_start = nal.offset;
            }
            if (is_vcl_nal(nal.type)) {
                current_has_vcl = true;
                current_keyframe = current_keyframe || nal.type == 5u;
            }
        }
        finish_frame(bytes.size());
        return frames;
    }

    size_t pending_prefix_start = npos;
    for (const auto& nal : nals) {
        if (current_frame_start == npos) {
            current_frame_start = nal.offset;
        }

        if (is_vcl_nal(nal.type)) {
            if (current_has_vcl) {
                const size_t boundary = pending_prefix_start != npos ? pending_prefix_start : nal.offset;
                finish_frame(boundary);
                current_frame_start = boundary;
                current_has_vcl = false;
                current_keyframe = false;
                pending_prefix_start = npos;
            }
            current_has_vcl = true;
            current_keyframe = current_keyframe || nal.type == 5u;
        } else if (current_has_vcl && pending_prefix_start == npos) {
            pending_prefix_start = nal.offset;
        }
    }
    finish_frame(bytes.size());
    return frames;
}

std::expected<H264VideoFrame, std::string> H264VideoReader::read_next_frame() {
    if (!has_frames()) {
        return std::unexpected("EOF");
    }

    const auto& range = m_frames[m_current_frame];
    const auto bytes = m_reader.subspan(range.offset, range.size);
    H264VideoFrame frame{
        .size = static_cast<uint32_t>(bytes.size()),
        .index = m_current_frame,
        .is_keyframe = range.is_keyframe,
        .data = bytes,
        .record_bytes = bytes,
    };
    ++m_current_frame;
    return frame;
}

} // namespace cricodecs::video
