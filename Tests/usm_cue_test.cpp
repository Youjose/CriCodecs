#include "usm/usm_container.hpp"
#include "adx/adx_codec.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cricodecs::usm;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> fixture(const char* path) {
    std::ifstream input(path);
    require(input.is_open(), "could not open CUE fixture");
    std::vector<uint8_t> bytes;
    unsigned value;
    while (input >> std::hex >> value) {
        require(value <= 255, "invalid CUE fixture byte");
        bytes.push_back(static_cast<uint8_t>(value));
    }
    require(input.eof(), "invalid CUE fixture");
    return bytes;
}

void check_points(const UsmCueMetadata& metadata) {
    constexpr std::array<uint64_t, 8> times = {1, 41000, 76814, 99327, 108250, 109843, 110343, 113113};
    constexpr std::array<const char*, 8> names = {
        "Rumble=2000.000_80.000_80.000_6", "Rumble=2000.000_80.000_80.000_6",
        "Rumble=2000.000_80.000_80.000_6", "Rumble=2000.000_60.000_60.000_6",
        "Rumble=2000.000_60.000_60.000_6", "Trigger=PreEnd",
        "Trigger=stopVideoRendering", "Trigger=End",
    };
    require(metadata.points.size() == times.size(), "incorrect cue count");
    for (size_t i = 0; i < times.size(); ++i) {
        require(metadata.points[i].time == times[i], "incorrect cue time");
        require(metadata.points[i].name == names[i], "incorrect cue name");
        require(metadata.points[i].cue_type == 1, "incorrect cue type");
        require(metadata.points[i].parameter.empty(), "unexpected cue parameter");
    }
}

const UsmChunk& cue_chunk(const UsmReader& reader) {
    for (const auto& chunk : reader.chunks()) {
        if (chunk.chunk_type() == UsmChunkType::CUE &&
            chunk.payload_type() == UsmPayloadType::Metadata) return chunk;
    }
    throw std::runtime_error("CUE chunk missing");
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("cricodecs-cue-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ivf");
    const auto audio_path = path.string() + ".adx";
    const auto subtitle_path = path.string() + ".srt";
    try {
        const auto raw = fixture(CUE_FIXTURE_PATH);
        require(raw.size() == 576, "incorrect fixture size");
        // The supplied header omitted the last 32 bytes of its declared padding.
        const auto raw_header = fixture(CUE_HEADER_FIXTURE_PATH);
        require(raw_header.size() == 256, "incorrect header fixture size");

        // A minimal IVF record exercises the actual USM builder and seek table.
        constexpr std::array<uint8_t, 45> video = {
            'D','K','I','F', 0,0, 32,0, 'V','P','9','0', 16,0, 16,0,
            30,0,0,0, 1,0,0,0, 1,0,0,0, 0,0,0,0,
            1,0,0,0, 0,0,0,0, 0,0,0,0, 0x80,
        };
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(video.data()), video.size());
            require(out.good(), "could not create IVF fixture");
        }

        UsmBuildInput input;
        input.video_path = path;
        input.cue_metadata_chunks = {raw_header, raw};
        UsmBuilder builder;
        auto output = builder.build(input);
        require(output.has_value(), "could not build USM with preserved CUE");
        UsmReader reader;
        require(reader.load(*output).has_value(), "could not read rebuilt USM");
        require(cue_chunk(reader).pack() == raw, "preserved CUE differs from sample");
        std::vector<uint8_t> expected_cue_stream;
        size_t cue_stream_index = reader.streams().size();
        for (size_t i = 0; i < reader.streams().size(); ++i) {
            if (reader.streams()[i].stream_id == UsmChunkType::CUE) {
                cue_stream_index = i;
            }
        }
        require(cue_stream_index < reader.streams().size(), "CUE stream missing from CRID");
        for (const auto& current : reader.chunks()) {
            if (current.chunk_type() == UsmChunkType::CUE) {
                const auto packed = current.pack();
                expected_cue_stream.insert(expected_cue_stream.end(), packed.begin(), packed.end());
            }
        }
        auto extracted_cue = reader.extract_stream(static_cast<uint32_t>(cue_stream_index));
        require(extracted_cue.has_value() && *extracted_cue == expected_cue_stream,
                "CUE stream extraction did not include its packed chunks");
        auto cue_sample = reader.extract_stream_sample(static_cast<uint32_t>(cue_stream_index), 24);
        require(cue_sample.has_value() &&
                *cue_sample == std::vector<uint8_t>(expected_cue_stream.begin(), expected_cue_stream.begin() + 24),
                "CUE stream preview sample is incorrect");
        auto demuxed = reader.demux();
        require(demuxed.has_value() && demuxed->contains(reader.describe_stream(reader.streams()[cue_stream_index].id())) &&
                demuxed->at(reader.describe_stream(reader.streams()[cue_stream_index].id())) == expected_cue_stream,
                "CUE demux output is empty or incorrect");
        const auto cue_extract_path = path.string() + ".cue";
        require(reader.extract_file(static_cast<uint32_t>(cue_stream_index), cue_extract_path).has_value(),
                "could not extract CUE stream to file");
        {
            std::ifstream extracted_file(cue_extract_path, std::ios::binary);
            const std::vector<uint8_t> extracted_bytes{
                std::istreambuf_iterator<char>(extracted_file), std::istreambuf_iterator<char>()};
            require(extracted_bytes == expected_cue_stream, "extracted CUE file is empty or incorrect");
        }
        std::filesystem::remove(cue_extract_path);
        const auto& chunks = reader.chunks();
        require(chunks.size() >= 10, "missing USM chunks");
        require(chunks[0].chunk_type() == UsmChunkType::CRID, "CRID not first");
        require(chunks[1].chunk_type() == UsmChunkType::SFV &&
                chunks[1].payload_type() == UsmPayloadType::Header, "video header missing");
        require(chunks[2].pack() == raw_header, "CUE header was not preserved in header section");
        require(chunks[3].chunk_type() == UsmChunkType::SFV &&
                chunks[3].payload_type() == UsmPayloadType::SectionEnd, "video header end missing");
        require(chunks[4].chunk_type() == UsmChunkType::CUE &&
                chunks[4].payload_type() == UsmPayloadType::SectionEnd, "CUE header end missing");
        size_t cue_metadata_index = 5;
        while (cue_metadata_index < chunks.size() && chunks[cue_metadata_index].pack() != raw) {
            ++cue_metadata_index;
        }
        require(cue_metadata_index < chunks.size() - 3, "CUE metadata missing");
        require(chunks[cue_metadata_index - 1].chunk_type() == UsmChunkType::SFV &&
                chunks[cue_metadata_index - 1].payload_type() == UsmPayloadType::Metadata,
                "video seek metadata must precede CUE metadata");
        require(chunks[cue_metadata_index + 1].chunk_type() == UsmChunkType::SFV &&
                chunks[cue_metadata_index + 1].payload_type() == UsmPayloadType::SectionEnd,
                "video metadata end missing");
        require(chunks[cue_metadata_index + 2].chunk_type() == UsmChunkType::CUE &&
                chunks[cue_metadata_index + 2].payload_type() == UsmPayloadType::SectionEnd,
                "CUE metadata end missing");
        require(chunks.back().chunk_type() == UsmChunkType::CUE &&
                chunks.back().payload_type() == UsmPayloadType::SectionEnd,
                "CUE contents end missing");
        require(reader.find_stream({UsmChunkType::CUE, 0}) != nullptr, "CUE directory entry missing");
        require(cue_chunk(reader).header.frame_rate == 30, "incorrect sample CUE header");
        auto parsed = reader.cue_metadata();
        require(parsed.has_value() && parsed->size() == 1, "could not parse sample CUE");
        check_points(parsed->front());

        input.cue_metadata_chunks.clear();
        input.cue_metadata = *parsed;
        auto regenerated = builder.build(input);
        require(regenerated.has_value(), "could not regenerate CUE");
        UsmReader regenerated_reader;
        require(regenerated_reader.load(*regenerated).has_value(), "could not read regenerated USM");
        auto regenerated_cues = regenerated_reader.cue_metadata();
        require(regenerated_cues.has_value() && regenerated_cues->size() == 1, "could not read regenerated CUE");
        check_points(regenerated_cues->front());
        const auto& chunk = cue_chunk(regenerated_reader);
        require(chunk.header.magic == 0x40435545 && chunk.payload_type() == UsmPayloadType::Metadata,
                "incorrect generated CUE header");
        require(chunk.header.frame_rate == 30, "incorrect generated CUE frame rate");
        const auto& generated_chunks = regenerated_reader.chunks();
        require(generated_chunks[2].pack() == raw_header, "generated CUE header differs from sample");
        auto generated_header = generated_chunks[2].load_utf_payload();
        require(generated_header.has_value() && generated_header->table_name() == "CUEPOINT_HDRINFO",
                "generated CUE header missing");
        require(generated_header->get<uint32_t>(0, "time_unit") == 1000u &&
                generated_header->get<uint32_t>(0, "total_points") == 8u &&
                generated_header->get<uint32_t>(0, "metadata_count") == 1u &&
                generated_header->get<uint32_t>(0, "metadata_size") == chunk.packed_size() &&
                generated_header->get<uint32_t>(0, "content_xsize") == 0u &&
                generated_header->get<uint32_t>(0, "ixsize") == 0u,
                "incorrect generated CUE header values");
        input.cue_metadata_chunks = {raw_header};
        require(!builder.build(input).has_value(), "generated metadata accepted with a preserved CUE header");
        input.cue_metadata_chunks.clear();
        auto table = chunk.load_utf_payload();
        require(table.has_value() && table->table_name() == "CUEPOINT_INFO", "incorrect CUE table");
        auto name = table->get_data(0, "name");
        require(name.has_value() && name->size() == 33 && (*name)[31] == 0 && (*name)[32] == 0,
                "incorrect CUE name bytes");

        input.cue_metadata[0].points[0].parameter = {1, 2, 3};
        auto with_parameter = builder.build(input);
        require(with_parameter.has_value(), "could not build CUE parameter");
        UsmReader parameter_reader;
        require(parameter_reader.load(*with_parameter).has_value(), "could not read CUE parameter file");
        auto parameters = parameter_reader.cue_metadata();
        require(parameters.has_value() && parameters->front().points[0].parameter == std::vector<uint8_t>({1, 2, 3}),
                "CUE parameter did not round trip");

        input.cue_metadata.clear();
        input.cue_metadata_chunks.clear();
        for (const auto& existing : reader.chunks()) {
            if (existing.chunk_type() == UsmChunkType::CUE) {
                auto bytes = existing.pack();
                bytes[12] = 7;
                if (existing.payload_type() == UsmPayloadType::SectionEnd) bytes[23] = 25;
                input.cue_metadata_chunks.push_back(std::move(bytes));
            }
        }
        auto rebuilt = builder.build(input);
        require(rebuilt.has_value(), "could not rebuild all CUE chunks");
        UsmReader rebuilt_reader;
        require(rebuilt_reader.load(*rebuilt).has_value(), "could not read rebuilt CUE chunks");
        std::vector<std::vector<uint8_t>> rebuilt_cues;
        for (const auto& existing : rebuilt_reader.chunks()) {
            if (existing.chunk_type() == UsmChunkType::CUE) {
                rebuilt_cues.push_back(existing.pack());
            }
        }
        require(rebuilt_cues == input.cue_metadata_chunks, "CUE chunks did not preserve their order and bytes");
        require(rebuilt_reader.find_stream({UsmChunkType::CUE, 7}) != nullptr, "CUE channel directory entry missing");
        input.cue_metadata_chunks.push_back(input.cue_metadata_chunks.back());
        require(!builder.build(input).has_value(), "duplicate CUE end marker was accepted");
        input.cue_metadata_chunks.pop_back();

        constexpr std::array<int16_t, 64> pcm{};
        require(cricodecs::adx::AdxEncoder::encode_to_file(
                    audio_path, pcm, cricodecs::adx::AdxEncodeConfig{}).has_value(),
                "could not create ADX fixture");
        {
            std::ofstream subtitle(subtitle_path);
            subtitle << "1\n00:00:00,000 --> 00:00:01,000\nHello\n";
            require(subtitle.good(), "could not create subtitle fixture");
        }
        input.audio_tracks.push_back(UsmBuildInput::AudioTrack{.path = audio_path});
        input.subtitle_tracks.push_back(UsmBuildInput::SubtitleTrack{.path = subtitle_path});
        auto mixed = builder.build(input);
        require(mixed.has_value(), "could not build mixed-stream USM");
        UsmReader mixed_reader;
        require(mixed_reader.load(*mixed).has_value(), "could not read mixed-stream USM");
        std::vector<std::string> phases;
        uint64_t first_video_offset = 0;
        uint64_t offset = 0;
        std::optional<uint64_t> seek_offset;
        for (const auto& current : mixed_reader.chunks()) {
            const auto type = current.chunk_type();
            if (current.payload_type() == UsmPayloadType::Stream) {
                if (type == UsmChunkType::SFV && first_video_offset == 0) first_video_offset = offset;
            } else if (current.payload_type() == UsmPayloadType::SectionEnd) {
                const std::string marker(current.payload.begin(), current.payload.end());
                const auto end = marker.find(" END");
                require(end != std::string::npos, "unknown end marker");
                phases.push_back(std::to_string(static_cast<uint32_t>(type)) + marker.substr(0, end + 4));
            } else if (current.is_utf_payload()) {
                auto table = current.load_utf_payload();
                require(table.has_value(), "invalid UTF table in mixed-stream USM");
                phases.push_back(std::to_string(static_cast<uint32_t>(type)) + std::string(table->table_name()));
                if (table->table_name() == "VIDEO_SEEKINFO") {
                    seek_offset = table->get<uint64_t>(0, "ofs_byte").value();
                }
            }
            offset += current.packed_size();
        }
        const auto id = [](UsmChunkType type) { return std::to_string(static_cast<uint32_t>(type)); };
        require(phases == std::vector<std::string>{
                    id(UsmChunkType::CRID) + "CRIUSF_DIR_STREAM",
                    id(UsmChunkType::SFV) + "VIDEO_HDRINFO",
                    id(UsmChunkType::SFA) + "AUDIO_HDRINFO",
                    id(UsmChunkType::SBT) + "SUBTITLE_HDRINFO",
                    id(UsmChunkType::CUE) + "CUEPOINT_HDRINFO",
                    id(UsmChunkType::SFV) + "#HEADER END",
                    id(UsmChunkType::SFA) + "#HEADER END",
                    id(UsmChunkType::SBT) + "#HEADER END",
                    id(UsmChunkType::CUE) + "#HEADER END",
                    id(UsmChunkType::SFV) + "VIDEO_SEEKINFO",
                    id(UsmChunkType::CUE) + "CUEPOINT_INFO",
                    id(UsmChunkType::SFV) + "#METADATA END",
                    id(UsmChunkType::CUE) + "#METADATA END",
                    id(UsmChunkType::SFV) + "#CONTENTS END",
                    id(UsmChunkType::SFA) + "#CONTENTS END",
                    id(UsmChunkType::SBT) + "#CONTENTS END",
                    id(UsmChunkType::CUE) + "#CONTENTS END",
                }, "incorrect mixed-stream USM phase order");
        require(seek_offset.has_value() && *seek_offset == first_video_offset, "video seek offset is incorrect");
        input.audio_tracks.clear();
        input.subtitle_tracks.clear();

        auto unusual_markers = input.cue_metadata_chunks;
        unusual_markers[3][15] = static_cast<uint8_t>(UsmPayloadType::Metadata);
        unusual_markers[4][15] = static_cast<uint8_t>(UsmPayloadType::Stream);
        input.cue_metadata_chunks = unusual_markers;
        auto unusual_output = builder.build(input);
        require(unusual_output.has_value(), "could not preserve CUE markers with alternate payload types");
        UsmReader unusual_reader;
        require(unusual_reader.load(*unusual_output).has_value(), "could not read alternate CUE markers");
        require(unusual_reader.chunks().back().pack() == unusual_markers.back(),
                "CUE contents marker was not last");
        auto unusual_cues = unusual_reader.cue_metadata();
        require(unusual_cues.has_value() && unusual_cues->size() == 1,
                "CUE metadata end marker was parsed as a table");

        input.cue_metadata_chunks = {raw};
        input.cue_streams = {UsmStreamInfo{
            .filename = "events.cue",
            .filename_raw = "events.cue",
            .stream_id = UsmChunkType::CUE,
            .channel_no = 0,
            .fmtver = 16777984,
        }};
        auto with_directory_entry = builder.build(input);
        require(with_directory_entry.has_value(), "could not build CUE directory entry");
        UsmReader directory_reader;
        require(directory_reader.load(*with_directory_entry).has_value(), "could not read CUE directory entry");
        const auto* stream = directory_reader.find_stream({UsmChunkType::CUE, 0});
        require(stream != nullptr && stream->filename == "events.cue", "CUE directory entry was lost");
        input.cue_streams.push_back(input.cue_streams.front());
        require(!builder.build(input).has_value(), "duplicate CUE directory entry was accepted");
        input.cue_streams.clear();

        input.cue_metadata_chunks = {std::vector<uint8_t>(raw.begin(), raw.end() - 1)};
        require(!builder.build(input).has_value(), "truncated CUE was accepted");
        std::filesystem::remove(path);
        std::filesystem::remove(audio_path);
        std::filesystem::remove(subtitle_path);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::filesystem::remove(audio_path);
        std::filesystem::remove(subtitle_path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
