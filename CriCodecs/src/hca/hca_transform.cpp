/**
 * @file hca_transform.cpp
 * @brief Shared HCA DCT4 transform implementation.
 *
 * The staged DCT-IV shape is HCA-specific and was compared against the public
 * decoder/encoder implementations.
 */

#include "hca_transform.hpp"
#include "../utilities/numeric.hpp"

#include <cstddef>
#include <numbers>

namespace cricodecs::hca::transform {

namespace {

template <int... Bits>
struct PackedDctTrigRows {
    static constexpr size_t value_count = ((size_t{1} << Bits) + ...);

    std::array<float, value_count> values{};
};

consteval int bit_reverse(int value, int bits) noexcept {
    int reversed = 0;
    for (int i = 0; i < bits; ++i) {
        reversed = (reversed << 1) | ((value >> i) & 1);
    }
    return reversed;
}

template <int Bits>
consteval float dct4_angle(int index) noexcept {
    constexpr int size = 1 << Bits;
    return std::numbers::pi_v<float> * static_cast<float>(4 * index + 1) / static_cast<float>(4 * size);
}

template <int Bits, int... RowBits>
consteval void fill_sine_rows(PackedDctTrigRows<RowBits...>& rows, size_t& offset) {
    constexpr int size = 1 << Bits;
    for (int i = 0; i < size; ++i) {
        rows.values[offset + static_cast<size_t>(i)] = util::sin(dct4_angle<Bits>(i));
    }
    offset += size;
}

template <int Bits, int... RowBits>
consteval void fill_cosine_rows(PackedDctTrigRows<RowBits...>& rows, size_t& offset) {
    constexpr int size = 1 << Bits;
    for (int i = 0; i < size; ++i) {
        rows.values[offset + static_cast<size_t>(i)] = util::cos(dct4_angle<Bits>(i));
    }
    offset += size;
}

template <int... Bits>
consteval PackedDctTrigRows<Bits...> generate_hca_dct4_sine_rows() {
    PackedDctTrigRows<Bits...> rows{};
    size_t offset = 0;
    (fill_sine_rows<Bits>(rows, offset), ...);
    return rows;
}

template <int... Bits>
consteval PackedDctTrigRows<Bits...> generate_hca_dct4_cosine_rows() {
    PackedDctTrigRows<Bits...> rows{};
    size_t offset = 0;
    (fill_cosine_rows<Bits>(rows, offset), ...);
    return rows;
}

template <int Bits>
consteval std::array<int, 1 << Bits> generate_shuffle_row() {
    std::array<int, 1 << Bits> row{};
    for (int i = 0; i < (1 << Bits); ++i) {
        row[static_cast<size_t>(i)] = bit_reverse(i ^ (i / 2), Bits);
    }
    return row;
}

// HCA's 128-point DCT4 uses trig rows 7, 5, 4, 3, 2, 1, and 0.
// The staged transform never uses trig row 6, and only the bit-7 shuffle row is needed.
inline constexpr auto HCA_DCT4_SINE_ROWS = generate_hca_dct4_sine_rows<7, 5, 4, 3, 2, 1, 0>();
inline constexpr auto HCA_DCT4_COSINE_ROWS = generate_hca_dct4_cosine_rows<7, 5, 4, 3, 2, 1, 0>();
inline constexpr auto HCA_DCT4_SHUFFLE_ROW = generate_shuffle_row<HCA_MDCT_BITS>();

template <int Bits>
[[nodiscard]] constexpr const float* sine_row() noexcept {
    if constexpr (Bits == 7) {
        return HCA_DCT4_SINE_ROWS.values.data();
    } else if constexpr (Bits == 5) {
        return HCA_DCT4_SINE_ROWS.values.data() + 128;
    } else if constexpr (Bits == 4) {
        return HCA_DCT4_SINE_ROWS.values.data() + 160;
    } else if constexpr (Bits == 3) {
        return HCA_DCT4_SINE_ROWS.values.data() + 176;
    } else if constexpr (Bits == 2) {
        return HCA_DCT4_SINE_ROWS.values.data() + 184;
    } else if constexpr (Bits == 1) {
        return HCA_DCT4_SINE_ROWS.values.data() + 188;
    } else {
        static_assert(Bits == 0);
        return HCA_DCT4_SINE_ROWS.values.data() + 190;
    }
}

template <int Bits>
[[nodiscard]] constexpr const float* cosine_row() noexcept {
    if constexpr (Bits == 7) {
        return HCA_DCT4_COSINE_ROWS.values.data();
    } else if constexpr (Bits == 5) {
        return HCA_DCT4_COSINE_ROWS.values.data() + 128;
    } else if constexpr (Bits == 4) {
        return HCA_DCT4_COSINE_ROWS.values.data() + 160;
    } else if constexpr (Bits == 3) {
        return HCA_DCT4_COSINE_ROWS.values.data() + 176;
    } else if constexpr (Bits == 2) {
        return HCA_DCT4_COSINE_ROWS.values.data() + 184;
    } else if constexpr (Bits == 1) {
        return HCA_DCT4_COSINE_ROWS.values.data() + 188;
    } else {
        static_assert(Bits == 0);
        return HCA_DCT4_COSINE_ROWS.values.data() + 190;
    }
}

template <int Bits>
constexpr void initial_rotation(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& input,
    std::array<float, HCA_SAMPLES_PER_SUBFRAME>& temp
) {
    constexpr int size = 1 << Bits;
    const float* sine = sine_row<Bits>();
    const float* cosine = cosine_row<Bits>();

    for (int i = 0; i < size / 2; ++i) {
        const int i2 = i * 2;
        const float a = input[static_cast<size_t>(i2)];
        const float b = input[static_cast<size_t>(size - 1 - i2)];
        const float sin = sine[static_cast<size_t>(i)];
        const float cos = cosine[static_cast<size_t>(i)];
        temp[static_cast<size_t>(i2)] = a * cos + b * sin;
        temp[static_cast<size_t>(i2 + 1)] = a * sin - b * cos;
    }
}

template <int HalfBits>
constexpr void stage_pass(std::array<float, HCA_SAMPLES_PER_SUBFRAME>& temp) {
    constexpr int block_half_size = 1 << HalfBits;
    constexpr int block_size = block_half_size * 2;
    constexpr int block_count = HCA_SAMPLES_PER_SUBFRAME / (block_size * 2);
    const float* sine = sine_row<HalfBits>();
    const float* cosine = cosine_row<HalfBits>();

    for (int block = 0; block < block_count; ++block) {
        for (int i = 0; i < block_half_size; ++i) {
            const int front_pos = (block * block_size + i) * 2;
            const int back_pos = front_pos + block_size;
            const float a = temp[static_cast<size_t>(front_pos)] - temp[static_cast<size_t>(back_pos)];
            const float b = temp[static_cast<size_t>(front_pos + 1)] - temp[static_cast<size_t>(back_pos + 1)];
            const float sin = sine[static_cast<size_t>(i)];
            const float cos = cosine[static_cast<size_t>(i)];

            temp[static_cast<size_t>(front_pos)] += temp[static_cast<size_t>(back_pos)];
            temp[static_cast<size_t>(front_pos + 1)] += temp[static_cast<size_t>(back_pos + 1)];
            temp[static_cast<size_t>(back_pos)] = a * cos + b * sin;
            temp[static_cast<size_t>(back_pos + 1)] = a * sin - b * cos;
        }
    }
}

[[nodiscard]] constexpr std::array<float, HCA_SAMPLES_PER_SUBFRAME> finish_shuffle(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& temp,
    const float scale
) {
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> output{};
    for (int i = 0; i < HCA_SAMPLES_PER_SUBFRAME; ++i) {
        output[static_cast<size_t>(i)] =
            temp[static_cast<size_t>(HCA_DCT4_SHUFFLE_ROW[static_cast<size_t>(i)])] * scale;
    }
    return output;
}

} // namespace

std::array<float, HCA_SAMPLES_PER_SUBFRAME> dct4(
    const std::array<float, HCA_SAMPLES_PER_SUBFRAME>& input,
    const float scale
) {
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> temp{};

    initial_rotation<HCA_MDCT_BITS>(input, temp);
    stage_pass<5>(temp);
    stage_pass<4>(temp);
    stage_pass<3>(temp);
    stage_pass<2>(temp);
    stage_pass<1>(temp);
    stage_pass<0>(temp);

    return finish_shuffle(temp, scale);
}

} // namespace cricodecs::hca::transform
