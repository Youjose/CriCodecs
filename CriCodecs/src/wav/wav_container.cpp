/**
 * @file wav_container.cpp
 * @brief RIFF/WAVE reader and writer helpers.
 *
 * WAV handling is support code for PCM interchange in CriCodecs bindings and
 * codec wrappers. Implemented by Youjose.
 */

#include "wav_container.hpp"
#include "../utilities/numeric.hpp"

#include <bit>
#include <limits>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace cricodecs::wav {

    static constexpr uint32_t RIFF_MAGIC = 0x46464952;
    static constexpr uint32_t WAVE_MAGIC = 0x45564157;
    static constexpr uint32_t FMT_MAGIC  = 0x20746D66;
    static constexpr uint32_t DATA_MAGIC = 0x61746164;
    static constexpr uint32_t SMPL_MAGIC = 0x6C706D73;
    static constexpr uint32_t CUE_MAGIC  = 0x20657563;

    static constexpr uint16_t WAVE_FORMAT_PCM        = 0x0001;
    static constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;
    static constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

    [[nodiscard]] constexpr size_t storage_bytes_for_bits(uint16_t bits) noexcept {
        return (static_cast<size_t>(bits) + 7) / 8;
    }

    [[nodiscard]] constexpr bool checked_range(size_t offset, size_t size, size_t limit) noexcept {
        return offset <= limit && size <= limit - offset;
    }

    void append_bytes(std::vector<uint8_t>& output, std::span<const uint8_t> bytes) {
        output.insert(output.end(), bytes.begin(), bytes.end());
    }

    template <typename T>
    [[nodiscard]] T read_le_unaligned(const uint8_t* data) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            return std::bit_cast<float>(io::read_le<uint32_t>(data));
        } else if constexpr (std::is_same_v<T, double>) {
            return std::bit_cast<double>(io::read_le<uint64_t>(data));
        } else {
            return io::read_le<T>(data);
        }
    }

    [[nodiscard]] constexpr int16_t float_to_pcm16(double sample) noexcept {
        if (!std::isfinite(sample)) {
            return 0;
        }
        if (sample <= -1.0) {
            return std::numeric_limits<int16_t>::lowest();
        }
        const double clamped = cricodecs::util::clamp(sample, -1.0, 1.0);
        return cricodecs::util::clamp_to<int16_t>(static_cast<int32_t>(clamped * 32767.0));
    }

    [[nodiscard]] constexpr int16_t pcm8_to_pcm16(uint8_t sample, uint16_t source_bits) noexcept {
        const int midpoint = source_bits < 8 ? (1 << (source_bits - 1)) : 0x80;
        return static_cast<int16_t>((static_cast<int>(sample) - midpoint) << 8);
    }

    [[nodiscard]] constexpr int16_t signed_pcm_to_pcm16(int32_t sample, uint16_t source_bits) noexcept {
        if (source_bits <= 16) {
            return cricodecs::util::clamp_to<int16_t>(sample);
        }
        return static_cast<int16_t>(sample >> (source_bits - 16));
    }

    struct WavWriteLayout {
        uint32_t data_size = 0;
        uint32_t fmt_size = 16;
        uint32_t smpl_size = 0;
        uint32_t riff_size = 0;
        uint16_t block_align = 0;
        uint32_t byte_rate = 0;
    };

    std::expected<WavWriteLayout, std::string> make_write_layout(
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        if (channels == 0 || sample_rate == 0) {
            return std::unexpected(std::string("WAV write failed: sample rate and channel count must be non-zero"));
        }
        if (channels > std::numeric_limits<uint16_t>::max() / sizeof(int16_t)) {
            return std::unexpected(std::string("WAV write failed: channel count is too large"));
        }
        if (pcm_data.size() > std::numeric_limits<uint32_t>::max() / sizeof(int16_t)) {
            return std::unexpected(std::string("WAV write failed: PCM data is too large for RIFF WAVE"));
        }

        WavWriteLayout layout;
        layout.data_size = static_cast<uint32_t>(pcm_data.size() * sizeof(int16_t));
        layout.block_align = static_cast<uint16_t>(channels * sizeof(int16_t));
        if (sample_rate > std::numeric_limits<uint32_t>::max() / layout.block_align) {
            return std::unexpected(std::string("WAV write failed: byte rate is too large"));
        }
        layout.byte_rate = sample_rate * layout.block_align;

        if (!loops.empty()) {
            if (loops.size() > (std::numeric_limits<uint32_t>::max() - 36) / 24) {
                return std::unexpected(std::string("WAV write failed: too many sample loops"));
            }
            layout.smpl_size = 36 + static_cast<uint32_t>(loops.size()) * 24;
        }

        uint64_t riff_size = 4ull + (8ull + layout.fmt_size) + (8ull + layout.data_size);
        if (!loops.empty()) {
            riff_size += 8ull + layout.smpl_size;
        }
        if (riff_size > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected(std::string("WAV write failed: PCM data is too large for RIFF WAVE"));
        }
        layout.riff_size = static_cast<uint32_t>(riff_size);

        return layout;
    }

    template <typename WriteU16, typename WriteU32>
    void emit_wave_header(
        WriteU16&& write_u16,
        WriteU32&& write_u32,
        const WavWriteLayout& layout,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        write_u32(RIFF_MAGIC);
        write_u32(layout.riff_size);
        write_u32(WAVE_MAGIC);

        write_u32(FMT_MAGIC);
        write_u32(layout.fmt_size);
        write_u16(WAVE_FORMAT_PCM);
        write_u16(channels);
        write_u32(sample_rate);
        write_u32(layout.byte_rate);
        write_u16(layout.block_align);
        write_u16(16);

        if (!loops.empty()) {
            write_u32(SMPL_MAGIC);
            write_u32(layout.smpl_size);

            write_u32(0);
            write_u32(0);
            write_u32(sample_rate > 0 ? 1000000000 / sample_rate : 0);
            write_u32(60);
            write_u32(0);
            write_u32(0);
            write_u32(0);
            write_u32(static_cast<uint32_t>(loops.size()));
            write_u32(0);

            for (const auto& loop : loops) {
                write_u32(loop.cue_point_id);
                write_u32(loop.type);
                write_u32(loop.start);
                write_u32(loop.end);
                write_u32(loop.fraction);
                write_u32(loop.play_count);
            }
        }

        write_u32(DATA_MAGIC);
        write_u32(layout.data_size);
    }

    [[nodiscard]] std::span<const uint8_t> pcm_bytes(std::span<const int16_t> pcm_data) noexcept {
        return {
            reinterpret_cast<const uint8_t*>(pcm_data.data()),
            pcm_data.size() * sizeof(int16_t)
        };
    }

    template <typename Convert>
    std::expected<void, std::string> convert_pcm_payload(
        std::span<const uint8_t> source,
        std::span<int16_t> target,
        size_t frame_count,
        uint16_t channels,
        uint16_t block_align,
        size_t sample_bytes,
        Convert convert)
    {
        if (sample_bytes == 0 || channels == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }
        if (frame_count > std::numeric_limits<size_t>::max() / channels) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }
        if (target.size() != frame_count * channels) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        const size_t tight_block_align = static_cast<size_t>(channels) * sample_bytes;
        if (block_align == tight_block_align) {
            const size_t bytes_needed = target.size() * sample_bytes;
            if (bytes_needed > source.size()) {
                return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
            }

            const uint8_t* src = source.data();
            for (int16_t& sample : target) {
                sample = convert(src);
                src += sample_bytes;
            }
            return {};
        }

        if (frame_count > std::numeric_limits<size_t>::max() / block_align) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }
        const size_t bytes_needed = frame_count * static_cast<size_t>(block_align);
        if (bytes_needed > source.size() || block_align < tight_block_align) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        size_t out = 0;
        const uint8_t* frame = source.data();
        for (size_t i = 0; i < frame_count; ++i) {
            const uint8_t* src = frame;
            for (uint16_t ch = 0; ch < channels; ++ch) {
                target[out++] = convert(src);
                src += sample_bytes;
            }
            frame += block_align;
        }
        return {};
    }

    std::expected<void, std::string> convert_pcm16_cache(
        std::span<const uint8_t> source,
        std::span<int16_t> target,
        size_t frame_count,
        uint16_t channels,
        uint16_t block_align,
        uint16_t compression,
        uint16_t storage_bits,
        uint16_t valid_bits)
    {
        const size_t sample_bytes = storage_bytes_for_bits(storage_bits);
        if (sample_bytes == 0 || sample_bytes > 8) {
            return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
        }

        const bool tight_frames = block_align == static_cast<size_t>(channels) * sample_bytes;
        if (compression == WAVE_FORMAT_PCM && valid_bits > 8 && valid_bits <= 16 &&
            sample_bytes == sizeof(int16_t) && tight_frames) {
            const size_t bytes_needed = target.size() * sizeof(int16_t);
            if (bytes_needed > source.size()) {
                return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
            }
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(target.data(), source.data(), bytes_needed);
            } else {
                const auto result = convert_pcm_payload(
                    source, target, frame_count, channels, block_align, sample_bytes,
                    [](const uint8_t* src) { return read_le_unaligned<int16_t>(src); });
                if (!result) return result;
            }
            return {};
        }

        if (compression == WAVE_FORMAT_IEEE_FLOAT) {
            if (storage_bits == 32) {
                return convert_pcm_payload(
                    source, target, frame_count, channels, block_align, sample_bytes,
                    [](const uint8_t* src) { return float_to_pcm16(read_le_unaligned<float>(src)); });
            }
            if (storage_bits == 64) {
                return convert_pcm_payload(
                    source, target, frame_count, channels, block_align, sample_bytes,
                    [](const uint8_t* src) { return float_to_pcm16(read_le_unaligned<double>(src)); });
            }
            return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
        }

        if (compression != WAVE_FORMAT_PCM) {
            return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
        }

        if (valid_bits <= 8) {
            return convert_pcm_payload(
                source, target, frame_count, channels, block_align, sample_bytes,
                [valid_bits](const uint8_t* src) { return pcm8_to_pcm16(*src, valid_bits); });
        }
        if (valid_bits <= 16 && sample_bytes == sizeof(int16_t)) {
            return convert_pcm_payload(
                source, target, frame_count, channels, block_align, sample_bytes,
                [](const uint8_t* src) { return read_le_unaligned<int16_t>(src); });
        }
        if (valid_bits <= 24 && sample_bytes == 3) {
            return convert_pcm_payload(
                source, target, frame_count, channels, block_align, sample_bytes,
                [valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(read_le_unaligned<io::Int24>(src), valid_bits);
                });
        }
        if (valid_bits <= 32 && sample_bytes == 4) {
            return convert_pcm_payload(
                source, target, frame_count, channels, block_align, sample_bytes,
                [valid_bits](const uint8_t* src) {
                    return signed_pcm_to_pcm16(read_le_unaligned<int32_t>(src), valid_bits);
                });
        }

        return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
    }

    std::expected<int16_t, std::string> read_pcm16_sample(
        std::span<const uint8_t> source,
        size_t index,
        size_t frame_count,
        uint16_t channels,
        uint16_t block_align,
        uint16_t compression,
        uint16_t storage_bits,
        uint16_t valid_bits)
    {
        if (channels == 0 || frame_count > std::numeric_limits<size_t>::max() / channels ||
            index >= frame_count * channels) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        const size_t sample_bytes = storage_bytes_for_bits(storage_bits);
        const size_t tight_block_align = static_cast<size_t>(channels) * sample_bytes;
        if (sample_bytes == 0 || sample_bytes > 8 || block_align < tight_block_align) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        const size_t frame = index / channels;
        const size_t channel = index % channels;
        if (frame > std::numeric_limits<size_t>::max() / block_align) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }
        const size_t offset = frame * static_cast<size_t>(block_align) + channel * sample_bytes;
        if (offset > source.size() || sample_bytes > source.size() - offset) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        const uint8_t* sample = source.data() + offset;
        if (compression == WAVE_FORMAT_IEEE_FLOAT) {
            if (storage_bits == 32) return float_to_pcm16(read_le_unaligned<float>(sample));
            if (storage_bits == 64) return float_to_pcm16(read_le_unaligned<double>(sample));
            return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
        }

        if (compression != WAVE_FORMAT_PCM) {
            return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
        }

        if (valid_bits <= 8) return pcm8_to_pcm16(*sample, valid_bits);
        if (valid_bits <= 16 && sample_bytes == sizeof(int16_t)) return read_le_unaligned<int16_t>(sample);
        if (valid_bits <= 24 && sample_bytes == 3) {
            return signed_pcm_to_pcm16(read_le_unaligned<io::Int24>(sample), valid_bits);
        }
        if (valid_bits <= 32 && sample_bytes == 4) {
            return signed_pcm_to_pcm16(read_le_unaligned<int32_t>(sample), valid_bits);
        }

        return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
    }

    std::expected<void, std::string> WavContainer::load(const std::string& path) {
        return load(std::filesystem::path(path));
    }

    std::expected<void, std::string> WavContainer::load(const std::filesystem::path& path) {
        auto bytes = io::read_file_bytes(path, "WAV load failed");
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        auto res = load(std::move(*bytes));
        if (!res) {
            return res;
        }
        m_source_path = path;
        return {};
    }

    std::expected<void, std::string> WavContainer::load(std::vector<uint8_t>&& data) {
        m_source_path.clear();
        m_owned_source = std::move(data);
        auto res = m_reader.open(std::span<const uint8_t>(m_owned_source.data(), m_owned_source.size()));
        if (!res) return std::unexpected(std::string("WAV I/O failed"));
        return parse_headers();
    }

    std::expected<void, std::string> WavContainer::load(std::span<const uint8_t> data) {
        return load(std::vector<uint8_t>(data.begin(), data.end()));
    }

    std::expected<void, std::string> WavContainer::parse_headers() {
        if (m_reader.size() < 12) return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));

        m_pcm_offset = 0;
        m_pcm_size = 0;
        m_pcm_compression = 0;
        m_pcm_storage_bits = 0;
        m_pcm_valid_bits = 0;
        m_format = {};
        m_sampler = {};
        m_cues.clear();
        m_sample_count = 0;
        m_pcm16_cache.clear();
        m_pcm_converted = false;
        
        m_reader.seek(0);
        
        if (m_reader.read_le<uint32_t>() != RIFF_MAGIC) return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));
        const uint32_t full_size = m_reader.read_le<uint32_t>();
        if (m_reader.read_le<uint32_t>() != WAVE_MAGIC) return std::unexpected(std::string("WAV parse failed: invalid RIFF/WAVE header"));

        size_t sum_size = 4;
        bool has_fmt = false;

        while (sum_size < full_size && m_reader.remaining() >= 8) {
            const size_t chunk_start = m_reader.tell();
            const uint32_t sig = m_reader.read_le<uint32_t>();
            const uint32_t size = m_reader.read_le<uint32_t>();
            
            const size_t data_offset = m_reader.tell();
            size_t total_chunk_size = static_cast<size_t>(size) + 8;
            
            if ((size & 1) && (total_chunk_size + sum_size + 1 <= full_size)) {
                total_chunk_size += 1;
            }

            if (!checked_range(data_offset, size, m_reader.size())) {
                return std::unexpected(std::string("WAV I/O failed"));
            }

            switch (sig) {
            case FMT_MAGIC: {
                if (size < 16) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                if (m_reader.remaining() < size) return std::unexpected(std::string("WAV I/O failed"));
                
                m_format.compression_mode = m_reader.read_le<uint16_t>();
                m_format.channels = m_reader.read_le<uint16_t>();
                m_format.sample_rate = m_reader.read_le<uint32_t>();
                m_format.avg_bytes_per_sec = m_reader.read_le<uint32_t>();
                m_format.block_align = m_reader.read_le<uint16_t>();
                m_format.bit_depth = m_reader.read_le<uint16_t>();
                
                if (m_format.compression_mode == WAVE_FORMAT_EXTENSIBLE) {
                    if (size < 40) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                    m_format.extension_size = m_reader.read_le<uint16_t>();
                    if (m_format.extension_size < 22) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                    m_format.valid_bits_per_sample = m_reader.read_le<uint16_t>();
                    m_format.channel_mask = m_reader.read_le<uint32_t>();
                    m_format.sub_format.Data1 = m_reader.read_le<uint32_t>();
                    m_format.sub_format.Data2 = m_reader.read_le<uint16_t>();
                    m_format.sub_format.Data3 = m_reader.read_le<uint16_t>();
                    m_format.sub_format.Data4 = m_reader.read_le<uint64_t>();

                    if (m_format.sub_format.Data1 != WAVE_FORMAT_PCM && 
                        m_format.sub_format.Data1 != WAVE_FORMAT_EXTENSIBLE && 
                        m_format.sub_format.Data1 != WAVE_FORMAT_IEEE_FLOAT) {
                        return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
                    }
                }
                
                if (m_format.compression_mode != WAVE_FORMAT_PCM && 
                    m_format.compression_mode != WAVE_FORMAT_EXTENSIBLE && 
                    m_format.compression_mode != WAVE_FORMAT_IEEE_FLOAT) {
                    return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
                }
                has_fmt = true;
                break;
            }
            case SMPL_MAGIC: {
                if (size < 36) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));
                if (m_reader.remaining() < size) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));
                
                m_sampler.manufacturer = m_reader.read_le<uint32_t>();
                m_sampler.product = m_reader.read_le<uint32_t>();
                m_sampler.sample_period = m_reader.read_le<uint32_t>();
                m_sampler.midi_unity_note = m_reader.read_le<uint32_t>();
                m_sampler.midi_pitch_fraction = m_reader.read_le<uint32_t>();
                m_sampler.smpte_format = m_reader.read_le<uint32_t>();
                m_sampler.smpte_offset = m_reader.read_le<uint32_t>();
                uint32_t num_loops = m_reader.read_le<uint32_t>();
                uint32_t sampler_data_size = m_reader.read_le<uint32_t>();
                
                const uint64_t expected_size = 36ull + static_cast<uint64_t>(num_loops) * 24ull + sampler_data_size;
                if (size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid smpl loop data"));

                for (uint32_t i = 0; i < num_loops; ++i) {
                    SampleLoop loop;
                    loop.cue_point_id = m_reader.read_le<uint32_t>();
                    loop.type = m_reader.read_le<uint32_t>();
                    loop.start = m_reader.read_le<uint32_t>();
                    loop.end = m_reader.read_le<uint32_t>();
                    loop.fraction = m_reader.read_le<uint32_t>();
                    loop.play_count = m_reader.read_le<uint32_t>();
                    m_sampler.loops.push_back(loop);
                }
                if (sampler_data_size > 0) {
                    auto data_span = m_reader.read_bytes(sampler_data_size);
                    m_sampler.sampler_data.assign(data_span.begin(), data_span.end());
                }
                break;
            }
            case DATA_MAGIC: {
                m_pcm_offset = data_offset;
                m_pcm_size = size;
                break;
            }
            case CUE_MAGIC: {
                if (size < 4) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                uint32_t num_cues = m_reader.read_le<uint32_t>();
                const uint64_t expected_size = 4ull + static_cast<uint64_t>(num_cues) * 24ull;
                if (size < expected_size) return std::unexpected(std::string("WAV parse failed: invalid format data"));
                
                for (uint32_t i = 0; i < num_cues; ++i) {
                    CuePoint cp;
                    cp.name = m_reader.read_le<uint32_t>();
                    cp.position = m_reader.read_le<uint32_t>();
                    cp.chunk_id = m_reader.read_le<uint32_t>();
                    cp.chunk_start = m_reader.read_le<uint32_t>();
                    cp.block_start = m_reader.read_le<uint32_t>();
                    cp.sample_offset = m_reader.read_le<uint32_t>();
                    m_cues.push_back(cp);
                }
                break;
            }
            default:
                break;
            }

            sum_size += total_chunk_size;
            
            if (sum_size > full_size) return std::unexpected(std::string("WAV parse failed: chunk table exceeds RIFF size"));
            
            m_reader.seek(chunk_start + total_chunk_size);
        }

        if (!has_fmt) return std::unexpected(std::string("WAV parse failed: invalid format data"));
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        if (m_format.channels == 0 || m_format.sample_rate == 0 ||
            m_format.block_align == 0 || m_format.bit_depth == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        m_sample_count = m_pcm_size / m_format.block_align;

        // If we have cue points but no loops (smpl), convert cue points to a loop
        if (m_sampler.loops.empty() && !m_cues.empty()) {
            SampleLoop loop{};
            loop.cue_point_id = m_cues[0].name;
            loop.type = 0; // forward loop
            loop.start = m_cues[0].position;
            if (m_cues.size() >= 2) {
                loop.end = m_cues[1].position;
            } else {
                loop.end = static_cast<uint32_t>(m_sample_count);
            }
            loop.fraction = 0;
            loop.play_count = 0; // infinite
            m_sampler.loops.push_back(loop);
        }

        m_pcm_storage_bits = m_format.bit_depth;
        m_pcm_valid_bits = m_format.compression_mode == WAVE_FORMAT_EXTENSIBLE
            ? m_format.valid_bits_per_sample
            : m_format.bit_depth;
        m_pcm_compression = m_format.compression_mode == WAVE_FORMAT_EXTENSIBLE
            ? static_cast<uint16_t>(m_format.sub_format.Data1)
            : m_format.compression_mode;

        const size_t storage_bytes = storage_bytes_for_bits(m_pcm_storage_bits);
        if (storage_bytes == 0 || storage_bytes > 8 ||
            m_format.block_align < m_format.channels * storage_bytes ||
            m_pcm_valid_bits == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        if (m_pcm_valid_bits > m_pcm_storage_bits) {
            return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
        }

        if (m_pcm_compression == WAVE_FORMAT_IEEE_FLOAT) {
            if (m_pcm_storage_bits != 32 && m_pcm_storage_bits != 64) {
                return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
            }
        } else if (m_pcm_compression == WAVE_FORMAT_PCM) {
            if (m_pcm_storage_bits > 32) {
                return std::unexpected(std::string("WAV PCM bit depth does not match compression type"));
            }
        } else {
            return std::unexpected(std::string("WAV parse failed: unsupported compression mode"));
        }

        return {};
    }

    std::expected<int16_t, std::string> WavContainer::get_sample(size_t index) const {
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        if (m_format.channels == 0 ||
            m_sample_count > std::numeric_limits<size_t>::max() / m_format.channels) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }

        const auto pcm_bytes = m_reader.subspan(m_pcm_offset, m_pcm_size);
        if (pcm_bytes.size() != m_pcm_size) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        return read_pcm16_sample(
            pcm_bytes,
            index,
            m_sample_count,
            m_format.channels,
            m_format.block_align,
            m_pcm_compression,
            m_pcm_storage_bits,
            m_pcm_valid_bits);
    }
    
    std::expected<std::span<const int16_t>, std::string> WavContainer::get_pcm16() const {
        if (m_pcm_size == 0) return std::unexpected(std::string("WAV PCM data chunk is missing"));
        
        if (m_pcm_converted) {
            return std::span<const int16_t>(m_pcm16_cache);
        }
        
        if (m_format.channels == 0) {
            return std::unexpected(std::string("WAV parse failed: invalid format data"));
        }
        if (m_sample_count > std::numeric_limits<size_t>::max() / m_format.channels) {
            return std::unexpected(std::string("WAV PCM data is too large"));
        }

        const size_t total_samples = m_sample_count * m_format.channels;
        const auto pcm_bytes = m_reader.subspan(m_pcm_offset, m_pcm_size);
        if (pcm_bytes.size() != m_pcm_size) {
            return std::unexpected(std::string("WAV read failed: PCM data is out of bounds"));
        }

        m_pcm16_cache.resize(total_samples);

        auto convert_result = convert_pcm16_cache(
            pcm_bytes,
            std::span<int16_t>(m_pcm16_cache),
            m_sample_count,
            m_format.channels,
            m_format.block_align,
            m_pcm_compression,
            m_pcm_storage_bits,
            m_pcm_valid_bits);
        if (!convert_result) {
            m_pcm16_cache.clear();
            return std::unexpected(convert_result.error());
        }

        m_pcm_converted = true;
        
        return std::span<const int16_t>(m_pcm16_cache);
    }

    std::expected<std::vector<uint8_t>, std::string> WavContainer::build_bytes(
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }

        std::vector<uint8_t> output;
        output.reserve(static_cast<size_t>(layout->riff_size) + 8);
        emit_wave_header(
            [&](uint16_t value) { io::append_le<uint16_t>(output, value); },
            [&](uint32_t value) { io::append_le<uint32_t>(output, value); },
            *layout,
            sample_rate,
            channels,
            loops
        );
        append_bytes(output, pcm_bytes(pcm_data));

        return output;
    }

    std::expected<void, std::string> WavContainer::write(
        const std::string& path,
        std::span<const int16_t> pcm_data,
        uint32_t sample_rate,
        uint16_t channels,
        std::span<const SampleLoop> loops)
    {
        auto layout = make_write_layout(pcm_data, sample_rate, channels, loops);
        if (!layout) {
            return std::unexpected(layout.error());
        }

        io::writer writer;
        auto open_res = writer.open(std::filesystem::path(path));
        if (!open_res) {
            return std::unexpected(std::string("WAV write failed: could not open output file"));
        }

        emit_wave_header(
            [&](uint16_t value) { writer.write_le<uint16_t>(value); },
            [&](uint32_t value) { writer.write_le<uint32_t>(value); },
            *layout,
            sample_rate,
            channels,
            loops
        );
        writer.write_bytes(pcm_bytes(pcm_data));

        auto close_res = writer.close();
        if (!close_res) {
            return std::unexpected(std::string("WAV write failed"));
        }

        return {};
    }

}
