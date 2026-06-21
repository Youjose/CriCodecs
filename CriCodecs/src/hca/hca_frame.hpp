#pragma once
/**
 * @file hca_frame.hpp
 * @brief HCA per-frame and per-channel runtime state.
 */

#include "hca_header.hpp"

#include <array>
#include <cstdint>

namespace cricodecs::hca {

struct HcaChannel {
    ChannelType type = ChannelType::Discrete;
    uint8_t coded_count = 0;

    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> scalefactors{};
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> resolution{};
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> gain{};

    std::array<uint8_t, 8> hfr_scales{};
    std::array<uint8_t, HCA_SUBFRAMES> intensity{};

    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> spectra{};
    std::array<std::array<int, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> quantized_spectra{};
    std::array<std::array<float, HCA_SUBFRAMES>, HCA_SAMPLES_PER_SUBFRAME> scaled_spectra{};

    std::array<float, HCA_SAMPLES_PER_SUBFRAME> imdct_previous{};
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> temp{};
    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> wave{};

    uint8_t noise_count = 0;
    uint8_t valid_count = 0;
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> noises{};

    std::array<float, 8> hfr_group_averages{};
    int header_length_bits = 0;
    int scalefactor_delta_bits = 0;
};

struct HcaFrame {
    HcaHeader info;
    std::array<HcaChannel, 8> channels;
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> ath_curve{};

    int acceptable_noise_level = 0;
    int evaluation_boundary = 0;
    uint32_t random = 1;
};

} // namespace cricodecs::hca
