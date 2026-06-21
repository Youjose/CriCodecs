#include "binding_helpers.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/usm/usm_container.hpp"
#include "../../CriCodecs/src/utilities/text_encoding.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] cricodecs::text::EncodingOptions encoding_options_from_python(const nb::handle& encoding) {
    if (encoding.is_none()) {
        return {};
    }

    std::string name = nb::cast<std::string>(encoding);
    cricodecs::text::EncodingOptions options{std::move(name)};
    if (options.encoding->empty() || cricodecs::text::is_auto_encoding(options)) {
        return {};
    }
    return options;
}

[[nodiscard]] cricodecs::usm::UsmReader load_usm_any(const nb::object& source, const nb::object& encoding) {
    cricodecs::usm::UsmReader reader;
    reader.set_encoding(encoding_options_from_python(encoding));
    if (auto path = python_text_path(source)) {
        unwrap_expected(reader.load(std::filesystem::path(*path)));
        return reader;
    }
    auto borrowed = borrow_python_source(source);
    unwrap_expected(reader.load(borrowed.as_span()));
    return reader;
}

} // namespace

void bind_usm_module(nb::module_& module) {
    nb::enum_<cricodecs::usm::UsmChunkType>(module, "UsmChunkType")
        .value("CRID", cricodecs::usm::UsmChunkType::CRID)
        .value("SFSH", cricodecs::usm::UsmChunkType::SFSH)
        .value("AHX", cricodecs::usm::UsmChunkType::AHX)
        .value("ELM", cricodecs::usm::UsmChunkType::ELM)
        .value("ATP", cricodecs::usm::UsmChunkType::ATP)
        .value("PST", cricodecs::usm::UsmChunkType::PST)
        .value("SFV", cricodecs::usm::UsmChunkType::SFV)
        .value("SFA", cricodecs::usm::UsmChunkType::SFA)
        .value("ALP", cricodecs::usm::UsmChunkType::ALP)
        .value("CUE", cricodecs::usm::UsmChunkType::CUE)
        .value("SBT", cricodecs::usm::UsmChunkType::SBT)
        .value("STA", cricodecs::usm::UsmChunkType::STA)
        .value("USR", cricodecs::usm::UsmChunkType::USR);

    nb::class_<cricodecs::usm::UsmStreamInfo>(module, "UsmStreamInfo")
        .def_ro("filename", &cricodecs::usm::UsmStreamInfo::filename)
        .def_prop_ro("filename_raw", [](const cricodecs::usm::UsmStreamInfo& stream) {
            return to_python_bytes(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(stream.filename_raw.data()),
                stream.filename_raw.size()
            ));
        })
        .def_ro("stream_id", &cricodecs::usm::UsmStreamInfo::stream_id)
        .def_ro("channel_no", &cricodecs::usm::UsmStreamInfo::channel_no)
        .def_ro("fmtver", &cricodecs::usm::UsmStreamInfo::fmtver)
        .def_ro("filesize", &cricodecs::usm::UsmStreamInfo::filesize)
        .def_ro("minchk", &cricodecs::usm::UsmStreamInfo::minchk)
        .def_ro("minbuf", &cricodecs::usm::UsmStreamInfo::minbuf)
        .def_ro("avbps", &cricodecs::usm::UsmStreamInfo::avbps);

    nb::class_<cricodecs::usm::UsmBuildInput::AudioTrack>(module, "UsmMuxAudioTrack")
        .def(nb::init<>())
        .def("__init__", [](cricodecs::usm::UsmBuildInput::AudioTrack* self, const nb::object& path, bool encrypt) {
            cricodecs::usm::UsmBuildInput::AudioTrack track;
            track.path = require_python_path(path, "path");
            track.encrypt = encrypt;
            new (self) cricodecs::usm::UsmBuildInput::AudioTrack(std::move(track));
        }, nb::arg("path"), nb::arg("encrypt") = false)
        .def_prop_rw("path", [](const cricodecs::usm::UsmBuildInput::AudioTrack& self) {
            return self.path.generic_string();
        }, [](cricodecs::usm::UsmBuildInput::AudioTrack& self, const nb::object& path) {
            self.path = require_python_path(path, "path");
        })
        .def_rw("encrypt", &cricodecs::usm::UsmBuildInput::AudioTrack::encrypt);

    nb::class_<cricodecs::usm::UsmBuildInput>(module, "UsmMuxConfig")
        .def(nb::init<>())
        .def("__init__", [](
            cricodecs::usm::UsmBuildInput* self,
            const nb::object& video_path,
            const std::vector<cricodecs::usm::UsmBuildInput::AudioTrack>& audio_tracks,
            bool encrypt_audio,
            uint64_t key,
            const nb::object& encoding
        ) {
            cricodecs::usm::UsmBuildInput input;
            input.video_path = require_python_path(video_path, "video_path");
            input.audio_tracks = audio_tracks;
            input.encrypt_audio = encrypt_audio;
            input.key = key;
            input.encoding = encoding_options_from_python(encoding);
            new (self) cricodecs::usm::UsmBuildInput(std::move(input));
        },
            nb::arg("video_path"),
            nb::arg("audio_tracks") = std::vector<cricodecs::usm::UsmBuildInput::AudioTrack>{},
            nb::arg("encrypt_audio") = false,
            nb::arg("key") = static_cast<uint64_t>(0),
            nb::arg("encoding") = nb::none()
        )
        .def_prop_rw("video_path", [](const cricodecs::usm::UsmBuildInput& self) {
            return self.video_path.generic_string();
        }, [](cricodecs::usm::UsmBuildInput& self, const nb::object& path) {
            self.video_path = require_python_path(path, "video_path");
        })
        .def_rw("audio_tracks", &cricodecs::usm::UsmBuildInput::audio_tracks)
        .def_rw("encrypt_audio", &cricodecs::usm::UsmBuildInput::encrypt_audio)
        .def_rw("key", &cricodecs::usm::UsmBuildInput::key);

    nb::class_<cricodecs::usm::UsmReader>(module, "Usm")
        .def_static(
            "load",
            [](const std::string& path, const nb::object& encoding) {
                cricodecs::usm::UsmReader reader;
                reader.set_encoding(encoding_options_from_python(encoding));
                unwrap_expected(reader.load(std::filesystem::path(path)));
                return reader;
            },
            nb::arg("path"),
            nb::arg("encoding") = nb::none(),
            "Load a USM container from a filesystem path."
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data, const nb::object& encoding) {
                const auto data_view = borrow_python_bytes(data);
                cricodecs::usm::UsmReader reader;
                reader.set_encoding(encoding_options_from_python(encoding));
                unwrap_expected(reader.load(as_byte_span(data_view)));
                return reader;
            },
            nb::arg("data"),
            nb::arg("encoding") = nb::none(),
            "Load a USM container from raw bytes."
        )
        .def_prop_ro("source_path", [](const cricodecs::usm::UsmReader& self) {
            return self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
        })
        .def_prop_ro("container_filename", [](const cricodecs::usm::UsmReader& self) {
            return std::string(self.container_filename());
        })
        .def_prop_ro("stream_count", [](const cricodecs::usm::UsmReader& self) {
            return self.streams().size();
        })
        .def_prop_ro("streams", [](const cricodecs::usm::UsmReader& self) {
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            return streams;
        })
        .def("info", [](const cricodecs::usm::UsmReader& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("container_filename") = std::string(self.container_filename());
            info.attr("stream_count") = self.streams().size();
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            info.attr("streams") = streams;
            return info;
        })
        .def("stream", [](const cricodecs::usm::UsmReader& self, uint32_t index) {
            const auto& streams = self.streams();
            if (index >= streams.size()) {
                raise_value_error("USM stream index is out of range");
            }
            return streams[index];
        }, nb::arg("index"))
        .def(
            "stream_bytes",
            [](cricodecs::usm::UsmReader& self, uint32_t index) {
                return to_python_bytes(unwrap_expected(self.extract_stream(index)));
            },
            nb::arg("index")
        )
        .def(
            "extract_file",
            [](cricodecs::usm::UsmReader& self, uint32_t index, const nb::object& output_path) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path")));
            },
            nb::arg("index"),
            nb::arg("output_path")
        )
        .def(
            "extract",
            [](cricodecs::usm::UsmReader& self, const std::string& output_dir) {
                unwrap_expected(self.extract(std::filesystem::path(output_dir)));
            },
            nb::arg("output_dir")
        )
        .def(
            "demux",
            [](cricodecs::usm::UsmReader& self) {
                nb::dict streams;
                for (auto&& [name, bytes] : unwrap_expected(self.demux())) {
                    streams[nb::str(name.c_str())] =
                        to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
                }
                return streams;
            }
        );

    install_attr_repr(module, "UsmStreamInfo", {"filename", "stream_id", "channel_no", "fmtver", "filesize", "minchk", "minbuf", "avbps"});
    install_attr_repr(module, "UsmMuxAudioTrack", {"path", "encrypt"});
    install_attr_repr(module, "UsmMuxConfig", {"video_path", "audio_tracks", "encrypt_audio", "key"});
    install_attr_repr(module, "Usm", {"source_path", "container_filename", "stream_count", "streams"});

    module.def(
        "mux",
        [](const cricodecs::usm::UsmBuildInput& input) {
            cricodecs::usm::UsmBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("config")
    );
    module.def(
        "mux",
        [](const cricodecs::usm::UsmBuildInput& input, const nb::object& output_path) {
            cricodecs::usm::UsmBuilder builder;
            unwrap_expected(builder.build_to_file(require_python_path(output_path, "output_path"), input));
        },
        nb::arg("config"),
        nb::arg("output_path")
    );

    module.def(
        "mux",
        [](
            const std::string& video_path,
            const std::vector<std::string>& audio_paths,
            const std::vector<bool>& audio_encrypt,
            bool encrypt_audio,
            uint64_t key,
            const nb::object& encoding
        ) {
            if (audio_encrypt.size() != audio_paths.size()) {
                raise_value_error("USM mux audio_encrypt size must match audio_paths");
            }

            cricodecs::usm::UsmBuildInput input;
            input.video_path = std::filesystem::path(video_path);
            input.encoding = encoding_options_from_python(encoding);
            input.encrypt_audio = encrypt_audio;
            input.key = key;
            for (size_t index = 0; index < audio_paths.size(); ++index) {
                input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                    .path = std::filesystem::path(audio_paths[index]),
                    .encrypt = audio_encrypt[index],
                });
            }

            cricodecs::usm::UsmBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("video_path"),
        nb::arg("audio_paths") = std::vector<std::string>{},
        nb::arg("audio_encrypt") = std::vector<bool>{},
        nb::arg("encrypt_audio") = false,
        nb::arg("key") = static_cast<uint64_t>(0),
        nb::arg("encoding") = nb::none()
    );

    module.def(
        "mux_to_file",
        [](
            const std::string& output_path,
            const std::string& video_path,
            const std::vector<std::string>& audio_paths,
            const std::vector<bool>& audio_encrypt,
            bool encrypt_audio,
            uint64_t key,
            const nb::object& encoding
        ) {
            if (audio_encrypt.size() != audio_paths.size()) {
                raise_value_error("USM mux audio_encrypt size must match audio_paths");
            }

            cricodecs::usm::UsmBuildInput input;
            input.video_path = std::filesystem::path(video_path);
            input.encoding = encoding_options_from_python(encoding);
            input.encrypt_audio = encrypt_audio;
            input.key = key;
            for (size_t index = 0; index < audio_paths.size(); ++index) {
                input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                    .path = std::filesystem::path(audio_paths[index]),
                    .encrypt = audio_encrypt[index],
                });
            }

            cricodecs::usm::UsmBuilder builder;
            unwrap_expected(builder.build_to_file(std::filesystem::path(output_path), input));
        },
        nb::arg("output_path"),
        nb::arg("video_path"),
        nb::arg("audio_paths") = std::vector<std::string>{},
        nb::arg("audio_encrypt") = std::vector<bool>{},
        nb::arg("encrypt_audio") = false,
        nb::arg("key") = static_cast<uint64_t>(0),
        nb::arg("encoding") = nb::none()
    );

    module.def(
        "load",
        &load_usm_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "demux",
        [](cricodecs::usm::UsmReader& usm) {
            nb::dict streams;
            for (auto&& [name, bytes] : unwrap_expected(usm.demux())) {
                streams[nb::str(name.c_str())] =
                    to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
            }
            return streams;
        },
        nb::arg("source")
    );
    module.def(
        "demux",
        [](const nb::object& source, const nb::object& encoding) {
            auto usm = load_usm_any(source, encoding);
            nb::dict streams;
            for (auto&& [name, bytes] : unwrap_expected(usm.demux())) {
                streams[nb::str(name.c_str())] =
                    to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
            }
            return streams;
        },
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "extract",
        [](cricodecs::usm::UsmReader& usm, const nb::object& output_dir) {
            unwrap_expected(usm.extract(require_python_path(output_dir, "output_dir")));
        },
        nb::arg("source"),
        nb::arg("output_dir")
    );
    module.def(
        "extract",
        [](const nb::object& source, const nb::object& output_dir, const nb::object& encoding) {
            auto usm = load_usm_any(source, encoding);
            unwrap_expected(usm.extract(require_python_path(output_dir, "output_dir")));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python
