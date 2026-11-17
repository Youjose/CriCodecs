#include "usm/usm_container.hpp"

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

std::vector<uint8_t> fixture() {
    std::ifstream input(CUE_FIXTURE_PATH);
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
        if (chunk.chunk_type() == UsmChunkType::CUE) return chunk;
    }
    throw std::runtime_error("CUE chunk missing");
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("cricodecs-cue-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ivf");
    try {
        const auto raw = fixture();
        require(raw.size() == 576, "incorrect fixture size");

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
        input.cue_metadata_chunks = {raw};
        UsmBuilder builder;
        auto output = builder.build(input);
        require(output.has_value(), "could not build USM with preserved CUE");
        UsmReader reader;
        require(reader.load(*output).has_value(), "could not read rebuilt USM");
        require(cue_chunk(reader).pack() == raw, "preserved CUE differs from sample");
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
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
