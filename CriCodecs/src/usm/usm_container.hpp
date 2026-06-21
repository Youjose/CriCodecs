#pragma once
/**
 * @file usm_container.hpp
 * @brief USM/SofDec 2 chunked stream container API.
 *
 * Chunk IDs, metadata schemas, SFSH handling, and stream-header behavior are
 * grounded in Medianoche/SofDec 2 evidence.
 * Public C++23 surface by Youjose.
 */

#include <array>
#include <cstddef>
#include <optional>
#include <vector>
#include <string>
#include <cstdint>
#include <expected>
#include <flat_map>
#include <map>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../utf/utf_table.hpp"
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"
#include "../video/ivf.hpp"
#include "usm_crypto.hpp"

namespace cricodecs::usm {

// Confirmed against Medianoche/Sofdec 2. SFSH is a fixed SofDec header variant,
// while the uncommon @-prefixed metadata IDs are kept symbolic so demuxed
// streams remain inspectable before their full payload semantics are modeled.
// PST is a picture-size table, ELM is an element/index table, STA carries
// ofs_byte/ofs_frmid/num_skip/resv rows, and ATP carries pic_size rows.
enum class UsmChunkType : uint32_t {
    CRID = 0x43524944, // "CRID"
    SFSH = 0x53465348, // "SFSH"
    AHX  = 0x40414858, // "@AHX"
    ELM  = 0x40454C4D, // "@ELM"
    ATP  = 0x40505441, // "@ATP"
    PST  = 0x40505354, // "@PST"
    SFV  = 0x40534656, // "@SFV"
    SFA  = 0x40534641, // "@SFA"
    ALP  = 0x40414C50, // "@ALP"
    CUE  = 0x40435545, // "@CUE"
    SBT  = 0x40534254, // "@SBT"
    STA  = 0x40535441, // "@STA"
    USR  = 0x40555352, // "@USR"
};

enum class UsmPayloadType : uint8_t {
    Stream = 0,
    Header = 1,
    SectionEnd = 2,
    Metadata = 3,
};

enum class UsmAudioCodec : uint8_t {
    Adx = 2,
    Hca = 4,
};

[[nodiscard]] std::string_view audio_codec_name(uint8_t codec) noexcept;

struct UsmStreamId {
    UsmChunkType stream_id = UsmChunkType::CRID;
    uint8_t channel_no = 0;

    auto operator<=>(const UsmStreamId&) const = default;
};

struct UsmChunkHeader {
    static constexpr uint32_t encoded_header_size = 0x18;
    static constexpr uint32_t raw_header_size = 0x20;

    uint32_t magic = 0;
    uint32_t chunk_size = 0;
    uint8_t unk08 = 0;
    uint8_t offset = 0x18;
    uint16_t padding = 0;
    uint8_t channel_no = 0;
    uint8_t unk0d = 0;
    uint8_t unk0e = 0;
    uint8_t type = 0;
    uint32_t frame_time = 0;
    uint32_t frame_rate = 0;
    uint32_t unk18 = 0;
    uint32_t unk1c = 0;

    [[nodiscard]] uint32_t body_size() const noexcept {
        return chunk_size >= encoded_header_size ? chunk_size - encoded_header_size : 0;
    }

    [[nodiscard]] uint32_t data_offset() const noexcept {
        return offset >= encoded_header_size ? offset - encoded_header_size : 0;
    }
};

struct SfshHeader {
    static constexpr uint32_t raw_header_size = 0x40;
    static constexpr uint32_t payload_offset_value = 0x40;

    std::array<uint8_t, raw_header_size> raw{};
    uint16_t version = 0;
    uint16_t field_06 = 0;
    uint16_t field_08 = 0;
    uint16_t field_0a = 0;
    uint16_t field_0c = 0;
    uint32_t field_0e = 0;
    uint32_t field_12 = 0;
    uint32_t payload_size = 0;
    uint32_t codec_word = 0;
    uint16_t field_1e = 0;
    uint16_t field_20 = 0;

    [[nodiscard]] uint32_t payload_offset() const noexcept {
        return payload_offset_value;
    }

    [[nodiscard]] uint8_t codec_marker() const noexcept {
        return static_cast<uint8_t>(codec_word >> 24);
    }

    [[nodiscard]] uint8_t normalized_codec_marker() const noexcept {
        switch (codec_marker()) {
        case 4:
            return 10;
        case 5:
            return 12;
        case 6:
            return 14;
        default:
            return 0;
        }
    }
};

struct UsmPayload {
    std::vector<uint8_t> owned;
    std::span<const uint8_t> view;

    UsmPayload() = default;

    UsmPayload(std::span<const uint8_t> bytes)
        : view(bytes) {}

    UsmPayload(std::vector<uint8_t>&& bytes)
        : owned(std::move(bytes))
        , view(owned) {}

    UsmPayload(const UsmPayload& other)
        : owned(other.owned)
        , view(owned.empty() ? other.view : std::span<const uint8_t>(owned)) {}

    UsmPayload& operator=(const UsmPayload& other) {
        if (this != &other) {
            owned = other.owned;
            view = owned.empty() ? other.view : std::span<const uint8_t>(owned);
        }
        return *this;
    }

    UsmPayload(UsmPayload&& other) noexcept
        : owned(std::move(other.owned))
        , view(owned.empty() ? other.view : std::span<const uint8_t>(owned)) {}

    UsmPayload& operator=(UsmPayload&& other) noexcept {
        if (this != &other) {
            owned = std::move(other.owned);
            view = owned.empty() ? other.view : std::span<const uint8_t>(owned);
        }
        return *this;
    }

    [[nodiscard]] const uint8_t* data() const noexcept { return view.data(); }
    [[nodiscard]] size_t size() const noexcept { return view.size(); }
    [[nodiscard]] bool empty() const noexcept { return view.empty(); }
    [[nodiscard]] const uint8_t* begin() const noexcept { return view.data(); }
    [[nodiscard]] const uint8_t* end() const noexcept { return view.data() + view.size(); }
    [[nodiscard]] const uint8_t& operator[](size_t index) const noexcept { return view[index]; }
    [[nodiscard]] operator std::span<const uint8_t>() const noexcept { return view; }
};

struct UsmChunk {
    UsmChunkHeader header;
    UsmPayload payload;
    UsmPayload padding;

    [[nodiscard]] UsmChunkType chunk_type() const noexcept {
        return static_cast<UsmChunkType>(header.magic);
    }

    [[nodiscard]] UsmStreamId stream_id() const noexcept {
        return UsmStreamId{
            .stream_id = chunk_type(),
            .channel_no = header.channel_no,
        };
    }

    [[nodiscard]] UsmPayloadType payload_type() const noexcept {
        return static_cast<UsmPayloadType>(header.type & 0x03u);
    }

    [[nodiscard]] bool is_stream() const noexcept {
        return payload_type() == UsmPayloadType::Stream;
    }

    [[nodiscard]] bool is_utf_payload() const noexcept {
        return payload.size() >= 4 &&
            payload[0] == '@' &&
            payload[1] == 'U' &&
            payload[2] == 'T' &&
            payload[3] == 'F';
    }

    [[nodiscard]] bool belongs_to(UsmStreamId id) const noexcept {
        return stream_id() == id;
    }

    [[nodiscard]] std::expected<utf::UtfTable, std::string> load_utf_payload() const {
        return utf::UtfTable::load(payload);
    }

    [[nodiscard]] std::vector<uint8_t> pack() const {
        std::vector<uint8_t> bytes(UsmChunkHeader::raw_header_size, 0);
        io::write_be<uint32_t>(bytes.data() + 0x00, header.magic);
        io::write_be<uint32_t>(bytes.data() + 0x04, header.chunk_size);
        bytes[0x08] = header.unk08;
        bytes[0x09] = header.offset;
        io::write_be<uint16_t>(bytes.data() + 0x0A, header.padding);
        bytes[0x0C] = header.channel_no;
        bytes[0x0D] = header.unk0d;
        bytes[0x0E] = header.unk0e;
        bytes[0x0F] = header.type;
        io::write_be<uint32_t>(bytes.data() + 0x10, header.frame_time);
        io::write_be<uint32_t>(bytes.data() + 0x14, header.frame_rate);
        io::write_be<uint32_t>(bytes.data() + 0x18, header.unk18);
        io::write_be<uint32_t>(bytes.data() + 0x1C, header.unk1c);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if (padding.size() == header.padding) {
            bytes.insert(bytes.end(), padding.begin(), padding.end());
        } else {
            bytes.resize(bytes.size() + header.padding, 0);
        }
        return bytes;
    }
};

struct UsmStreamInfo {
    std::string filename;
    std::string filename_raw;
    UsmChunkType stream_id = UsmChunkType::CRID;
    uint16_t channel_no = 0;
    uint32_t fmtver = 0;
    uint32_t filesize = 0;
    uint16_t minchk = 0;
    uint32_t minbuf = 0;
    uint32_t avbps = 0;

    [[nodiscard]] UsmStreamId id() const noexcept {
        return UsmStreamId{
            .stream_id = stream_id,
            .channel_no = static_cast<uint8_t>(channel_no),
        };
    }
};

struct UsmBuildInput {
    std::filesystem::path video_path;
    text::EncodingOptions encoding;

    struct AudioTrack {
        std::filesystem::path path;
        bool encrypt = false;
    };
    std::vector<AudioTrack> audio_tracks;

    bool encrypt_audio = false;
    uint64_t key = 0;
};

struct UsmPayloadView {
    std::string_view output_name;
    UsmStreamId stream_id;
    std::span<const uint8_t> payload;
};

class UsmReader {
public:
    UsmReader() = default;
    using AudioCodecMap = std::flat_map<uint16_t, uint8_t>;
    using OutputNameMap = std::flat_map<UsmStreamId, std::string>;

    std::expected<void, std::string> load(const std::filesystem::path& path);
    std::expected<void, std::string> load(std::span<const uint8_t> data);
    std::expected<std::map<std::string, std::vector<uint8_t>>, std::string> demux();
    template <class Callback>
    std::expected<void, std::string> visit_demuxed_payloads(Callback&& callback) const {
        auto&& callback_ref = callback;
        using CallbackRef = std::remove_reference_t<decltype(callback_ref)>;
        auto thunk = [](void* context, const UsmPayloadView& payload) -> std::expected<void, std::string> {
            auto& visitor = *static_cast<CallbackRef*>(context);
            if constexpr (std::is_void_v<std::invoke_result_t<CallbackRef&, const UsmPayloadView&>>) {
                std::invoke(visitor, payload);
                return {};
            } else {
                return std::invoke(visitor, payload);
            }
        };
        return visit_demuxed_payloads_impl(&callback_ref, thunk);
    }
    void set_key(uint64_t key) { m_crypto.init_key(key); }
    void set_encoding(text::EncodingOptions options) {
        m_encoding = std::move(options);
        m_output_names_ready = false;
        m_output_names.clear();
        m_output_name_error.clear();
    }

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] std::string_view container_filename() const noexcept { return m_container_filename; }
    [[nodiscard]] const utf::UtfTable& crid_header() const noexcept { return m_crid_header; }
    [[nodiscard]] const std::optional<SfshHeader>& sfsh_header() const noexcept { return m_sfsh_header; }
    [[nodiscard]] const std::vector<UsmStreamInfo>& streams() const noexcept { return m_streams; }
    [[nodiscard]] const std::vector<UsmChunk>& chunks() const noexcept { return m_chunks; }
    [[nodiscard]] const UsmStreamInfo* find_stream(UsmStreamId id) const noexcept;
    [[nodiscard]] const UsmStreamInfo* find_stream(const UsmChunk& chunk) const noexcept {
        return find_stream(chunk.stream_id());
    }
    [[nodiscard]] std::string describe_stream(UsmStreamId id) const;
    [[nodiscard]] std::string describe_stream(const UsmChunk& chunk) const {
        return describe_stream(chunk.stream_id());
    }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_stream(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    );
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir);

private:
    using PayloadVisitor = std::expected<void, std::string> (*)(void*, const UsmPayloadView&);

    std::filesystem::path m_source_path;
    std::vector<uint8_t> m_owned_source;
    io::reader m_reader;
    std::string m_container_filename;
    utf::UtfTable m_crid_header;
    std::optional<SfshHeader> m_sfsh_header;
    std::vector<UsmStreamInfo> m_streams;
    std::vector<UsmChunk> m_chunks;
    UsmCrypto m_crypto;
    text::EncodingOptions m_encoding;
    mutable OutputNameMap m_output_names;
    mutable std::string m_output_name_error;
    mutable bool m_output_names_ready = false;

    std::expected<void, std::string> parse_file();
    std::expected<void, std::string> parse_sfsh_file();
    [[nodiscard]] std::expected<const OutputNameMap*, std::string> output_name_map() const;
    [[nodiscard]] AudioCodecMap collect_audio_codecs() const;
    [[nodiscard]] bool chunk_needs_masking(const UsmChunk& chunk, const AudioCodecMap& audio_codecs) const;
    [[nodiscard]] std::vector<uint8_t> decrypt_chunk_payload(const UsmChunk& chunk) const;
    [[nodiscard]] std::expected<void, std::string> visit_demuxed_payloads_impl(
        void* context,
        PayloadVisitor visitor
    ) const;
    [[nodiscard]] std::expected<void, std::string> append_stream_payloads(
        UsmStreamId id,
        const AudioCodecMap& audio_codecs,
        std::vector<uint8_t>& output
    ) const;
    [[nodiscard]] std::expected<void, std::string> write_stream_payloads(
        UsmStreamId id,
        const AudioCodecMap& audio_codecs,
        const std::filesystem::path& output_path
    ) const;
};

class UsmBuilder {
public:
    UsmBuilder() = default;

    std::expected<std::vector<uint8_t>, std::string> build(const UsmBuildInput& input);
    std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const UsmBuildInput& input
    );

private:
    UsmCrypto m_crypto;
};

} // namespace cricodecs::usm
