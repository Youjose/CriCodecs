/**
 * @file adx_encoder.cpp
 * @brief ADX/AHX encode dispatch and ADX ADPCM frame writer.
 *
 * The ADX encoder began from VGAudio behavior, then was narrowed against CRI
 * adxencd for loop layout, header offsets, version behavior, and encryption
 * boundaries.
 *
 * Attribution:
 * - Initial encode reference: VGAudio.
 * - Current behavior checks: CRI adxencd.
 * - CriCodecs C++23 port and reverse-engineering follow-up by Youjose.
 */

#include "adx_codec.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace cricodecs::adx {

using cricodecs::util::divide_round_up;
using io::append_be;

    static constexpr uint16_t ADX_EOF_SCALE = 0x8001;
    static constexpr uint8_t ADX_FRAME_BYTES = 18;
    static constexpr uint8_t ADX_SCALE_BYTES = 2;
    static constexpr uint8_t ADX_NIBBLE_BYTES = ADX_FRAME_BYTES - ADX_SCALE_BYTES;
    static constexpr uint8_t ADX_BIT_DEPTH = 4;
    static constexpr uint8_t ADX_SAMPLES_PER_BLOCK = ADX_NIBBLE_BYTES * 2;
    static constexpr double PI = 3.141592653589793;
    static constexpr double SQRT2 = 1.414213562373095;

    [[nodiscard]] constexpr bool is_adx_encoding_mode(uint8_t mode) noexcept {
        return mode == 2 || mode == 3 || mode == 4;
    }

    struct AdxLoopLayout final {
        uint32_t alignment_samples = 0;
        uint32_t stored_data_offset = 0;
        uint32_t audio_offset = 0;
    };

    [[nodiscard]] static std::expected<std::vector<AdxLoop>, AdxError> normalize_official_loops(
        std::span<const AdxLoop> loops)
    {
        std::vector<AdxLoop> normalized;
        normalized.reserve(loops.size());
        for (const auto& loop : loops) {
            if (loop.start_sample > loop.end_sample) {
                return std::unexpected(AdxError("ADX loop start sample must not exceed end sample"));
            }
            if (loop.start_sample == loop.end_sample) {
                continue;
            }
            normalized.push_back(loop);
        }
        return normalized;
    }

    [[nodiscard]] static std::expected<AdxLoopLayout, AdxError> calculate_loop_layout(
        uint32_t header_struct_size,
        uint32_t samples_per_block,
        uint32_t frame_bytes,
        uint8_t channels,
        uint32_t original_sample_count,
        std::span<const AdxLoop> loops)
    {
        if (loops.empty()) {
            return AdxLoopLayout{};
        }

        const auto& first_loop = loops.front();
        if (first_loop.start_sample > first_loop.end_sample) {
            return std::unexpected(AdxError("ADX loop start sample must not exceed end sample"));
        }
        if (first_loop.end_sample > original_sample_count) {
            return std::unexpected(AdxError("ADX loop end sample exceeds source PCM length"));
        }

        const uint32_t alignment_unit = channels == 1 ? samples_per_block * 2 : samples_per_block;
        const uint32_t alignment_samples =
            alignment_unit == 0 ? 0 : (alignment_unit - (first_loop.start_sample % alignment_unit)) % alignment_unit;
        const uint32_t pre_loop_frames = (alignment_samples + first_loop.start_sample) / samples_per_block;
        const uint32_t pre_loop_bytes = pre_loop_frames * frame_bytes;
        // `adx_porting_notes.md` captures this writer formula as
        // align_up(header_base + pre_loop_bytes + 4, 0x800).
        const uint32_t loop_start_target = cricodecs::util::align_up(
            header_struct_size + pre_loop_bytes + 4, 0x800);

        return AdxLoopLayout{
            .alignment_samples = alignment_samples,
            .stored_data_offset = loop_start_target - pre_loop_bytes - 4,
            .audio_offset = loop_start_target - pre_loop_bytes,
        };
    }

    void AdxEncoder::calculate_coefficients(int32_t* coeffs, uint16_t highpass_freq, uint32_t sample_rate) {
        if (highpass_freq == 0 || sample_rate == 0) {
            coeffs[0] = 0;
            coeffs[1] = 0;
            return;
        }
        
        double a = SQRT2 - std::cos(2.0 * PI * highpass_freq / sample_rate);
        double b = SQRT2 - 1.0;
        double c = (a - std::sqrt((a + b) * (a - b))) / b;
        
        coeffs[0] = static_cast<int32_t>(c * 8192.0);
        coeffs[1] = static_cast<int32_t>(c * c * -4096.0);
    }

    static uint8_t combine_nibbles(int32_t high, int32_t low) {
        return static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
    }

    void AdxEncoder::encode_block(
        std::vector<uint8_t>& buffer,
        std::span<const int16_t> samples,
        int32_t* coeffs,
        AdpcmHistory& history,
        const AdxEncodeConfig& config,
        AdxKeyState* key_state
    ) {
        constexpr uint32_t samples_per_block = ADX_SAMPLES_PER_BLOCK;
        constexpr int32_t limit = (1 << (ADX_BIT_DEPTH - 1)) - 1;

        const size_t channel_stride = config.channels;
        const uint32_t available_samples = samples.empty()
            ? 0
            : static_cast<uint32_t>((samples.size() + channel_stride - 1) / channel_stride);
        const bool full_block = available_samples >= samples_per_block;
        const int16_t* sample_data = samples.data();

        const auto read_full_sample = [&](uint32_t index) -> int16_t {
            return sample_data[static_cast<size_t>(index) * channel_stride];
        };
        const auto read_padded_sample = [&](uint32_t index) -> int16_t {
            return index < available_samples ? read_full_sample(index) : 0;
        };

        int32_t minimum = 0, maximum = 0;
        const auto find_residual_bounds = [&](auto read_sample) {
            int16_t hist1 = history.prev1;
            int16_t hist2 = history.prev2;
            for (uint32_t i = 0; i < samples_per_block; ++i) {
                const int16_t current_sample = read_sample(i);
                int32_t sample = (((int32_t)current_sample << 12) - coeffs[0] * hist1 - coeffs[1] * hist2) >> 12;
                if (sample < minimum) minimum = sample;
                else if (sample > maximum) maximum = sample;
                hist2 = hist1;
                hist1 = current_sample;
            }
        };
        if (full_block) {
            find_residual_bounds(read_full_sample);
        } else {
            find_residual_bounds(read_padded_sample);
        }

        uint16_t scale;
        if (minimum == 0 && maximum == 0) {
            scale = 0;
            uint16_t scale_val = scale;
            if (key_state) {
                scale_val = (scale_val ^ key_state->xor_value) & 0x7FFF;
            }
            const size_t frame_offset = buffer.size();
            buffer.resize(frame_offset + ADX_FRAME_BYTES, 0);
            io::write_be<uint16_t>(buffer.data() + frame_offset, scale_val);
            {
                int16_t h1 = history.prev1;
                int16_t h2 = history.prev2;
                for (uint32_t i = 0; i < samples_per_block; ++i) {
                    int32_t predicted;
                    if (config.version == 4) {
                        predicted = (coeffs[0] * (int32_t)h1 + coeffs[1] * (int32_t)h2) >> 12;
                    } else {
                        predicted = (coeffs[0] * (int32_t)h1 >> 12) + (coeffs[1] * (int32_t)h2 >> 12);
                    }
                    const int32_t decoded = predicted;
                    int16_t clamped = static_cast<int16_t>(util::clamp(decoded, -32768, 32767));
                    h2 = h1;
                    h1 = clamped;
                }
                history.prev1 = h1;
                history.prev2 = h2;
            }
            return;
        }

        scale = static_cast<uint16_t>(
            maximum / limit > minimum / ~limit ? maximum / limit : minimum / ~limit
        );
        if (scale > 0x1000) scale = 0x1000;

        uint16_t scale_written;
        switch (config.encoding_mode) {
            case 4: {
                uint32_t power = std::bit_width(scale);
                scale = 1 << power;
                scale_written = static_cast<uint16_t>(12 - power);
                break;
            }
            case 2:
                scale_written = (config.filter_id << 13) | (scale & 0x1FFF);
                break;
            default:
                scale_written = scale;
                break;
        }

        uint16_t output_scale = scale_written;
        if (key_state) {
            output_scale = (output_scale ^ key_state->xor_value) & 0x7FFF;
        }

        int32_t decode_scale;
        switch (config.encoding_mode) {
            case 4:
                decode_scale = 1 << (12 - scale_written);
                break;
            case 2:
                decode_scale = (scale_written & 0x1FFF) + 1;
                break;
            default:
                decode_scale = (scale_written & 0x1FFF) + 1;
                break;
        }

        int16_t hist1 = history.prev1;
        int16_t hist2 = history.prev2;
        int16_t enc_scale = (scale == 0) ? 1 : scale;

        const auto emit_encoded_samples = [&](auto read_sample) {
            const size_t frame_offset = buffer.size();
            buffer.resize(frame_offset + ADX_FRAME_BYTES);
            uint8_t* frame = buffer.data() + frame_offset;
            io::write_be<uint16_t>(frame, output_scale);
            uint8_t* payload = frame + ADX_SCALE_BYTES;

            const auto encode_sample = [&](uint32_t index) -> int32_t {
                int32_t delta = (((int32_t)read_sample(index) << 12) - coeffs[0] * hist1 - coeffs[1] * hist2) >> 12;

                delta = delta > 0 ? delta + (enc_scale >> 1) : delta - (enc_scale >> 1);
                delta /= enc_scale;
                delta = util::clamp(delta, -8, 7);

                int32_t predicted;
                if (config.version == 4) {
                    predicted = (coeffs[0] * (int32_t)hist1 + coeffs[1] * (int32_t)hist2) >> 12;
                } else {
                    predicted = (coeffs[0] * (int32_t)hist1 >> 12) + (coeffs[1] * (int32_t)hist2 >> 12);
                }
                int32_t sim_sample = delta * decode_scale + predicted;
                int16_t decoded = static_cast<int16_t>(util::clamp(sim_sample, -32768, 32767));

                hist2 = hist1;
                hist1 = decoded;

                return delta;
            };

            for (uint32_t i = 0; i < ADX_NIBBLE_BYTES; ++i) {
                const int32_t high = encode_sample(i * 2);
                const int32_t low = encode_sample(i * 2 + 1);
                payload[i] = combine_nibbles(high, low);
            }
        };
        if (full_block) {
            emit_encoded_samples(read_full_sample);
        } else {
            emit_encoded_samples(read_padded_sample);
        }

        history.prev1 = hist1;
        history.prev2 = hist2;
    }

    std::expected<std::vector<uint8_t>, AdxError> AdxEncoder::encode(
        std::span<const int16_t> pcm_data,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        if (config.encoding_mode == 0x10 || config.encoding_mode == 0x11) {
            if (!loops.empty()) {
                return std::unexpected(AdxError("AHX encoding does not support loop metadata"));
            }

            ahx::AhxKey ahx_key = config.ahx_key;
            if (ahx_key.empty() && config.encryption_type == 0x09) {
                key9_derive(config.key64, config.subkey, ahx_key.start, ahx_key.mult, ahx_key.add);
            } else if (ahx_key.empty() && !config.key_string.empty()) {
                key8_derive(config.key_string, ahx_key.start, ahx_key.mult, ahx_key.add);
            }

            ahx::AhxEncodeConfig ahx_config{
                .encoding_mode = config.encoding_mode,
                .sample_rate = config.sample_rate,
                .channels = config.channels,
                .encryption_type = config.encryption_type,
                .key = ahx_key,
                .bit_allocation_pattern = config.ahx_bit_allocation_pattern,
            };
            return ahx::encode(pcm_data, ahx_config);
        }

        if (config.channels == 0 || config.sample_rate == 0) {
            return std::unexpected(AdxError("Invalid ADX encode configuration: sample rate and channels are required"));
        }
        if (!is_adx_encoding_mode(config.encoding_mode)) {
            return std::unexpected(AdxError("Unsupported ADX encoding mode"));
        }
        if (config.block_size != ADX_FRAME_BYTES) {
            return std::unexpected(AdxError("Unsupported ADX encode configuration: ADX frames must be 18 bytes"));
        }
        if (config.bit_depth != ADX_BIT_DEPTH) {
            return std::unexpected(AdxError("Unsupported ADX encode configuration: ADX samples must be 4-bit nibbles"));
        }
        if (config.encryption_type != 0 && config.encryption_type != 8 && config.encryption_type != 9) {
            return std::unexpected(AdxError("Unsupported ADX encryption"));
        }

        const auto normalized_loops_result = normalize_official_loops(loops);
        if (!normalized_loops_result.has_value()) {
            return std::unexpected(normalized_loops_result.error());
        }
        const auto& normalized_loops = normalized_loops_result.value();

        std::vector<uint8_t> buffer;
        
        constexpr uint32_t samples_per_block = ADX_SAMPLES_PER_BLOCK;
        
        const uint32_t source_samples_per_channel = static_cast<uint32_t>(pcm_data.size()) / config.channels;
        
        uint32_t header_struct_size = 20;

        if (config.version == 4) {
            size_t hist_count = (config.channels > 1) ? config.channels : 2;
            header_struct_size += 4 + static_cast<uint32_t>(hist_count * 4);
        }

        bool has_loops = !normalized_loops.empty();
        if (has_loops) {
            header_struct_size += 4 + static_cast<uint32_t>(normalized_loops.size() * 20);
        }

        const uint32_t frame_bytes = config.block_size * config.channels;
        const auto loop_layout_result = has_loops
            ? calculate_loop_layout(
                  header_struct_size,
                  samples_per_block,
                  frame_bytes,
                  config.channels,
                  source_samples_per_channel,
                  normalized_loops)
            : std::expected<AdxLoopLayout, AdxError>(AdxLoopLayout{});
        if (!loop_layout_result.has_value()) {
            return std::unexpected(loop_layout_result.error());
        }
        const AdxLoopLayout loop_layout = loop_layout_result.value();

        const uint32_t truncated_source_samples_per_channel =
            has_loops && config.delete_samples_after_loop_end
                ? normalized_loops.front().end_sample
                : source_samples_per_channel;

        std::vector<int16_t> padded_pcm;
        std::span<const int16_t> encoded_pcm = pcm_data;
        uint32_t samples_per_channel = truncated_source_samples_per_channel;
        if (has_loops && loop_layout.alignment_samples != 0) {
            samples_per_channel += loop_layout.alignment_samples;
            padded_pcm.assign(static_cast<size_t>(samples_per_channel) * config.channels, 0);
            for (uint32_t sample = 0; sample < truncated_source_samples_per_channel; ++sample) {
                for (uint32_t ch = 0; ch < config.channels; ++ch) {
                    padded_pcm[static_cast<size_t>(sample + loop_layout.alignment_samples) * config.channels + ch] =
                        pcm_data[static_cast<size_t>(sample) * config.channels + ch];
                }
            }
            encoded_pcm = padded_pcm;
        } else if (truncated_source_samples_per_channel != source_samples_per_channel) {
            encoded_pcm = pcm_data.first(static_cast<size_t>(truncated_source_samples_per_channel) * config.channels);
        }

        uint32_t blocks_per_channel = divide_round_up(samples_per_channel, samples_per_block);
        uint32_t frames = blocks_per_channel;

        const uint32_t audio_offset = has_loops
            ? loop_layout.audio_offset
            : cricodecs::util::align_up(header_struct_size + 6, 4);
        const uint32_t stored_data_offset = has_loops
            ? loop_layout.stored_data_offset
            : audio_offset - 4;

        const size_t encoded_audio_bytes = static_cast<size_t>(frames) * frame_bytes;
        buffer.reserve(static_cast<size_t>(audio_offset) + encoded_audio_bytes + config.block_size);

        append_be<uint16_t>(buffer, static_cast<uint16_t>(0x8000));
        append_be<uint16_t>(buffer, static_cast<uint16_t>(stored_data_offset));
        buffer.push_back(config.encoding_mode);
        buffer.push_back(config.block_size);
        buffer.push_back(config.bit_depth);
        buffer.push_back(config.channels);
        append_be<uint32_t>(buffer, config.sample_rate);
        append_be<uint32_t>(buffer, samples_per_channel);
        append_be<uint16_t>(buffer, config.highpass_freq);
        buffer.push_back(config.version);
        buffer.push_back(config.encryption_type);
        
        if (config.version == 4) {
            append_be<uint32_t>(buffer, 0);
            size_t hist_count = (config.channels > 1) ? config.channels : 2;
            for (size_t i = 0; i < hist_count; ++i) {
                append_be<uint16_t>(buffer, 0);
                append_be<uint16_t>(buffer, 0);
            }
        }

        if (has_loops) {
            append_be<uint16_t>(buffer, static_cast<uint16_t>(loop_layout.alignment_samples));
            append_be<uint16_t>(buffer, static_cast<uint16_t>(normalized_loops.size()));

            // TODO(adx): Partial-loop tail trimming and `-nodelterm` parity still
            // need direct official-tool coverage before widening the loop claim.
            for (size_t i = 0; i < normalized_loops.size(); ++i) {
                const auto& loop = normalized_loops[i];
                const uint32_t adjusted_start_sample = loop.start_sample + loop_layout.alignment_samples;
                const uint32_t adjusted_end_sample = loop.end_sample + loop_layout.alignment_samples;
                const uint32_t start_block = divide_round_up(adjusted_start_sample, samples_per_block);
                const uint32_t end_block = divide_round_up(adjusted_end_sample, samples_per_block);
                const uint32_t start_byte = audio_offset + start_block * frame_bytes;
                const uint32_t end_byte = audio_offset + end_block * frame_bytes;
                
                append_be<uint16_t>(buffer, loop.index);
                append_be<uint16_t>(buffer, loop.type == 0 ? 1 : loop.type);
                append_be<uint32_t>(buffer, adjusted_start_sample);
                append_be<uint32_t>(buffer, start_byte);
                append_be<uint32_t>(buffer, adjusted_end_sample);
                append_be<uint32_t>(buffer, end_byte);
            }
        }
        
        while (buffer.size() < audio_offset - 6) {
            buffer.push_back(0);
        }

        buffer.push_back('(');
        buffer.push_back('c');
        buffer.push_back(')');
        buffer.push_back('C');
        buffer.push_back('R');
        buffer.push_back('I');
        
        while (buffer.size() < audio_offset) {
            buffer.push_back(0);
        }

        AdxKeyState current_key_state;
        bool encrypted = (config.encryption_type == 8 || config.encryption_type == 9);
        if (encrypted) {
            if (config.encryption_type == 9) {
                key9_derive(config.key64, config.subkey, current_key_state.xor_value, current_key_state.mult, current_key_state.add);
            } else {
                key8_derive(config.key_string, current_key_state.xor_value, current_key_state.mult, current_key_state.add);
            }
        }

        int32_t coeffs[2];
        calculate_coefficients(coeffs, config.highpass_freq, config.sample_rate);
        
        std::vector<AdpcmHistory> histories(config.channels);
        
        if (config.version == 4 && samples_per_channel > 0) {
            for (uint32_t ch = 0; ch < config.channels; ++ch) {
                int16_t first = (ch < encoded_pcm.size()) ? encoded_pcm[ch] : 0;
                histories[ch].prev1 = first;
                histories[ch].prev2 = first;
            }
        }
        
        for (uint32_t b = 0; b < frames; ++b) {
            for (uint32_t ch = 0; ch < config.channels; ++ch) {
                size_t start_sample_idx = b * samples_per_block;
                size_t offset = start_sample_idx * config.channels + ch;
                
                std::span<const int16_t> block_span;
                if (offset < encoded_pcm.size()) {
                    block_span = encoded_pcm.subspan(offset);
                } else {
                    block_span = {};
                }
                
                encode_block(
                    buffer,
                    block_span,
                    coeffs,
                    histories[ch],
                    config,
                    encrypted ? &current_key_state : nullptr);
                
                if (encrypted) {
                    current_key_state.advance();
                }
            }
        }
        
        append_be<uint16_t>(buffer, ADX_EOF_SCALE);
        append_be<uint16_t>(buffer, static_cast<uint16_t>(config.block_size - 4));
        for (uint32_t i = 0; i < static_cast<uint32_t>(config.block_size - 4); ++i) {
            buffer.push_back(0);
        }
        
        return buffer;
    }

    std::expected<std::vector<uint8_t>, AdxError> AdxEncoder::encode(
        const wav::WavContainer& wav,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        auto pcm = wav.get_pcm16();
        if (!pcm) {
            return std::unexpected(AdxError("ADX encode failed: ") + pcm.error());
        }

        auto effective_config = config;
        effective_config.sample_rate = wav.sample_rate();
        effective_config.channels = static_cast<uint8_t>(wav.channels());

        std::vector<AdxLoop> wav_loops;
        std::span<const AdxLoop> effective_loops = loops;
        if (effective_loops.empty()) {
            const auto& source_loops = wav.sampler().loops;
            wav_loops.reserve(source_loops.size());
            for (const auto& loop : source_loops) {
                wav_loops.push_back(AdxLoop{
                    .index = static_cast<uint16_t>(loop.cue_point_id),
                    .type = static_cast<uint16_t>(loop.type),
                    .start_sample = loop.start,
                    .start_byte = 0,
                    .end_sample = loop.end,
                    .end_byte = 0,
                });
            }
            effective_loops = wav_loops;
        }

        return encode(*pcm, effective_config, effective_loops);
    }

    std::expected<void, AdxError> AdxEncoder::encode_to_file(
        const std::string& path,
        std::span<const int16_t> pcm_data,
        const AdxEncodeConfig& config,
        std::span<const AdxLoop> loops
    ) {
        auto result = encode(pcm_data, config, loops);
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        
        io::writer writer;
        if (auto open_result = writer.open(std::filesystem::path(path)); !open_result) {
            return std::unexpected(AdxError("Failed to open ADX output file for writing: ") + open_result.error());
        }
        
        if (auto write_result = writer.write(std::span<const uint8_t>(result.value())); !write_result) {
            return std::unexpected(AdxError("Failed to write ADX output file: ") + write_result.error());
        }
        
        if (auto close_result = writer.close(); !close_result) {
            return std::unexpected(AdxError("Failed to finalize ADX output file: ") + close_result.error());
        }
        
        return {};
    }

} // namespace cricodecs::adx
