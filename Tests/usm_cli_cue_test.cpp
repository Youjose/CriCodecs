#include "adx/adx_codec.hpp"
#include "cli/cli.hpp"
#include "usm/usm_container.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace cricodecs::usm;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
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

void write_bytes(const fs::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(out.good(), "could not write fixture");
}

size_t stream_index(const UsmReader& reader, UsmChunkType type, uint8_t channel) {
    for (size_t i = 0; i < reader.streams().size(); ++i) {
        if (reader.streams()[i].id() == UsmStreamId{type, channel}) return i;
    }
    throw std::runtime_error("stream not found");
}

std::vector<std::vector<uint8_t>> cue_chunks(const UsmReader& reader, uint8_t channel) {
    std::vector<std::vector<uint8_t>> result;
    for (const auto& chunk : reader.chunks()) {
        if (chunk.stream_id() == UsmStreamId{UsmChunkType::CUE, channel}) {
            result.push_back(chunk.pack());
        }
    }
    return result;
}

int run_cli(const std::vector<std::string>& args, std::string& error) {
    std::ostringstream out;
    std::ostringstream err;
    const int result = cricodecs::cli::run(args, out, err);
    error = err.str();
    return result;
}

struct TempDirectory {
    fs::path path;
    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

} // namespace

int main() {
    TempDirectory temp;
    try {
        const auto candidate = fs::temp_directory_path() /
            ("cricodecs-cli-cue-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(candidate), "could not create fixture directory");
        temp.path = candidate;
        const auto video_path = temp.path / "original.ivf";
        const auto replacement_path = temp.path / "replacement.ivf";
        const auto audio_path = temp.path / "audio.adx";
        const auto subtitle_path = temp.path / "subtitle.srt";
        const auto source_path = temp.path / "source.usm";
        const auto output_path = temp.path / "updated.usm";

        std::array<uint8_t, 45> video = {
            'D','K','I','F', 0,0, 32,0, 'V','P','9','0', 16,0, 16,0,
            30,0,0,0, 1,0,0,0, 1,0,0,0, 0,0,0,0,
            1,0,0,0, 0,0,0,0, 0,0,0,0, 0x80,
        };
        write_bytes(video_path, video);
        video[12] = 32;
        write_bytes(replacement_path, video);
        constexpr std::array<int16_t, 64> pcm{};
        require(cricodecs::adx::AdxEncoder::encode_to_file(
            audio_path.string(), pcm, cricodecs::adx::AdxEncodeConfig{}).has_value(), "could not create audio");
        {
            std::ofstream subtitle(subtitle_path);
            subtitle << "1\n00:00:00,000 --> 00:00:01,000\nHello\n";
            require(subtitle.good(), "could not create subtitle");
        }

        auto header = fixture(CUE_HEADER_FIXTURE_PATH);
        auto metadata = fixture(CUE_FIXTURE_PATH);
        auto header7 = header;
        auto metadata7 = metadata;
        header7[12] = 7;
        metadata7[12] = 7;
        UsmBuildInput input;
        input.video_path = video_path;
        input.audio_tracks.push_back({.path = audio_path, .channel_no = 3, .filename = "sound.adx"});
        input.subtitle_tracks.push_back({.path = subtitle_path, .channel_no = 5, .filename = "captions.sbt"});
        input.cue_metadata_chunks = {header, metadata, header7, metadata7};
        input.cue_streams = {
            {.filename = "events.cue", .filename_raw = "events.cue", .stream_id = UsmChunkType::CUE, .channel_no = 0},
            {.filename = "events7.cue", .filename_raw = "events7.cue", .stream_id = UsmChunkType::CUE, .channel_no = 7},
        };
        UsmBuilder builder;
        auto built = builder.build(input);
        require(built.has_value(), "could not build mixed USM");
        write_bytes(source_path, *built);
        UsmReader original;
        require(original.load(source_path).has_value(), "could not load mixed USM");
        const auto video_index = stream_index(original, UsmChunkType::SFV, 0);
        const auto cue_index = stream_index(original, UsmChunkType::CUE, 0);
        const auto audio_index = stream_index(original, UsmChunkType::SFA, 3);
        const auto subtitle_index = stream_index(original, UsmChunkType::SBT, 5);
        const auto original_audio = original.extract_stream(static_cast<uint32_t>(audio_index));
        const auto original_subtitle = original.extract_stream(static_cast<uint32_t>(subtitle_index));
        const auto original_video = original.extract_stream(static_cast<uint32_t>(video_index));
        require(original_audio && original_subtitle && original_video, "could not extract source streams");

        std::string error;
        const auto replace = std::to_string(video_index) + "=" + replacement_path.string();
        require(run_cli({source_path.string(), "--replace", replace, "-o", output_path.string()}, error) == 0,
                "CLI video replacement failed: " + error);
        UsmReader updated;
        require(updated.load(output_path).has_value(), "could not load mutated USM");
        for (const uint8_t channel : {uint8_t{0}, uint8_t{7}}) {
            require(cue_chunks(updated, channel) == cue_chunks(original, channel), "CUE chunks changed");
            const auto* old_entry = original.find_stream({UsmChunkType::CUE, channel});
            const auto* new_entry = updated.find_stream({UsmChunkType::CUE, channel});
            require(old_entry && new_entry && old_entry->filename == new_entry->filename, "CUE entry changed");
        }
        auto audio = updated.extract_stream(static_cast<uint32_t>(stream_index(updated, UsmChunkType::SFA, 3)));
        auto subtitle = updated.extract_stream(static_cast<uint32_t>(stream_index(updated, UsmChunkType::SBT, 5)));
        auto new_video = updated.extract_stream(static_cast<uint32_t>(stream_index(updated, UsmChunkType::SFV, 0)));
        require(audio && *audio == *original_audio, "audio changed");
        require(subtitle && *subtitle == *original_subtitle, "subtitle changed");
        require(new_video && *new_video != *original_video, "video was not replaced");

        const auto removed_path = temp.path / "removed.usm";
        require(run_cli({source_path.string(), "--replace", replace, "--remove", std::to_string(cue_index),
                         "-o", removed_path.string()}, error) == 0, "CLI cue removal failed: " + error);
        UsmReader removed;
        require(removed.load(removed_path).has_value(), "could not load cue removal output");
        require(!removed.find_stream({UsmChunkType::CUE, 0}) && cue_chunks(removed, 0).empty(),
                "removed CUE remained in output");
        require(cue_chunks(removed, 7) == cue_chunks(original, 7), "other CUE channel changed");

        require(run_cli({source_path.string(), "--replace", std::to_string(cue_index) + "=" + replacement_path.string(),
                         "-o", output_path.string()}, error) != 0 &&
                error.find("not supported") != std::string::npos, "CUE replacement should be rejected");

        auto extended = *built;
        auto location = std::search(extended.begin(), extended.end(), header.begin(), header.end());
        require(location != extended.end(), "could not locate CUE header");
        const auto offset = static_cast<size_t>(location - extended.begin());
        extended[offset + 7] += 4;
        extended[offset + 9] += 4;
        extended.insert(extended.begin() + static_cast<std::ptrdiff_t>(offset + 32), 4, 0);
        const auto extended_path = temp.path / "extended.usm";
        write_bytes(extended_path, extended);
        require(run_cli({extended_path.string(), "--replace", replace, "-o", output_path.string()}, error) != 0 &&
                error.find("extended header") != std::string::npos, "extended CUE header should be rejected");
        require(run_cli({extended_path.string(), "--replace", replace, "--remove", std::to_string(cue_index),
                         "-o", output_path.string()}, error) == 0, "removing extended CUE failed: " + error);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
