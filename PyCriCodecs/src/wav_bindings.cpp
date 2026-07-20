#include "binding_helpers.hpp"

#include <filesystem>
#include <vector>

#include "../../CriCodecs/src/wav/wav_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] nb::list loop_list(const std::vector<cricodecs::wav::SampleLoop>& loops) {
    nb::list result;
    for (const auto& loop : loops) {
        result.append(loop);
    }
    return result;
}

[[nodiscard]] nb::list cue_list(const std::vector<cricodecs::wav::CuePoint>& cues) {
    nb::list result;
    for (const auto& cue : cues) {
        result.append(cue);
    }
    return result;
}

[[nodiscard]] std::vector<int16_t> wav_pcm16_from_python_bytes(const nb::bytes& bytes) {
    const auto view = borrow_python_bytes(bytes);
    if ((view.size() % sizeof(int16_t)) != 0) {
        raise_value_error("WAV build failed: pcm16le input size must be an even number of bytes");
    }

    std::vector<int16_t> samples(view.size() / sizeof(int16_t));
    for (size_t index = 0; index < samples.size(); ++index) {
        const auto lo = static_cast<uint8_t>(view[(index * 2) + 0]);
        const auto hi = static_cast<uint8_t>(view[(index * 2) + 1]);
        const uint16_t raw = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
        samples[index] = static_cast<int16_t>(raw);
    }
    return samples;
}

[[nodiscard]] cricodecs::wav::WavContainer load_wav_any(const nb::object& source) {
    cricodecs::wav::WavContainer wav;
    if (auto path = python_text_path(source)) {
        unwrap_expected(wav.load(std::filesystem::path(*path)));
        return wav;
    }
    auto borrowed = borrow_python_source(source);
    unwrap_expected(wav.load(borrowed.as_span()));
    return wav;
}

} // namespace

void bind_wav_module(nb::module_& module) {
    nb::class_<cricodecs::wav::GUID>(module, "GUID")
        .def_ro("data1", &cricodecs::wav::GUID::Data1)
        .def_ro("data2", &cricodecs::wav::GUID::Data2)
        .def_ro("data3", &cricodecs::wav::GUID::Data3)
        .def_ro("data4", &cricodecs::wav::GUID::Data4);

    nb::class_<cricodecs::wav::SampleLoop>(module, "SampleLoop")
        .def(nb::init<>())
        .def_rw("cue_point_id", &cricodecs::wav::SampleLoop::cue_point_id)
        .def_rw("type", &cricodecs::wav::SampleLoop::type)
        .def_rw("start", &cricodecs::wav::SampleLoop::start)
        .def_rw("end", &cricodecs::wav::SampleLoop::end)
        .def_rw("fraction", &cricodecs::wav::SampleLoop::fraction)
        .def_rw("play_count", &cricodecs::wav::SampleLoop::play_count);

    nb::class_<cricodecs::wav::CuePoint>(module, "CuePoint")
        .def_ro("name", &cricodecs::wav::CuePoint::name)
        .def_ro("position", &cricodecs::wav::CuePoint::position)
        .def_ro("chunk_id", &cricodecs::wav::CuePoint::chunk_id)
        .def_ro("chunk_start", &cricodecs::wav::CuePoint::chunk_start)
        .def_ro("block_start", &cricodecs::wav::CuePoint::block_start)
        .def_ro("sample_offset", &cricodecs::wav::CuePoint::sample_offset);

    nb::class_<cricodecs::wav::SamplerChunk>(module, "SamplerChunk")
        .def_ro("manufacturer", &cricodecs::wav::SamplerChunk::manufacturer)
        .def_ro("product", &cricodecs::wav::SamplerChunk::product)
        .def_ro("sample_period", &cricodecs::wav::SamplerChunk::sample_period)
        .def_ro("midi_unity_note", &cricodecs::wav::SamplerChunk::midi_unity_note)
        .def_ro("midi_pitch_fraction", &cricodecs::wav::SamplerChunk::midi_pitch_fraction)
        .def_ro("smpte_format", &cricodecs::wav::SamplerChunk::smpte_format)
        .def_ro("smpte_offset", &cricodecs::wav::SamplerChunk::smpte_offset)
        .def_prop_ro("loops", [](const cricodecs::wav::SamplerChunk& self) {
            return loop_list(self.loops);
        })
        .def_prop_ro("sampler_data", [](const cricodecs::wav::SamplerChunk& self) {
            return to_python_bytes(self.sampler_data);
        });

    nb::class_<cricodecs::wav::WavFormat>(module, "WavFormat")
        .def_ro("compression_mode", &cricodecs::wav::WavFormat::compression_mode)
        .def_ro("channels", &cricodecs::wav::WavFormat::channels)
        .def_ro("sample_rate", &cricodecs::wav::WavFormat::sample_rate)
        .def_ro("avg_bytes_per_sec", &cricodecs::wav::WavFormat::avg_bytes_per_sec)
        .def_ro("block_align", &cricodecs::wav::WavFormat::block_align)
        .def_ro("bit_depth", &cricodecs::wav::WavFormat::bit_depth)
        .def_ro("extension_size", &cricodecs::wav::WavFormat::extension_size)
        .def_ro("valid_bits_per_sample", &cricodecs::wav::WavFormat::valid_bits_per_sample)
        .def_ro("channel_mask", &cricodecs::wav::WavFormat::channel_mask)
        .def_prop_ro(
            "sub_format",
            [](const cricodecs::wav::WavFormat& self) -> const cricodecs::wav::GUID& {
                return self.sub_format;
            },
            nb::rv_policy::reference_internal
        );

    nb::class_<cricodecs::wav::WavContainer>(module, "Wav")
        .def_static("load", &load_wav_any, nb::arg("source"))
        .def_static("load_bytes", [](const nb::bytes& data) {
            cricodecs::wav::WavContainer wav;
            unwrap_expected(wav.load(as_byte_span(borrow_python_bytes(data))));
            return wav;
        }, nb::arg("data"))
        .def_prop_ro("source_path", [](const cricodecs::wav::WavContainer& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro(
            "format",
            [](const cricodecs::wav::WavContainer& self) -> const cricodecs::wav::WavFormat& {
                return self.format();
            },
            nb::rv_policy::reference_internal
        )
        .def_prop_ro(
            "sampler",
            [](const cricodecs::wav::WavContainer& self) -> const cricodecs::wav::SamplerChunk& {
                return self.sampler();
            },
            nb::rv_policy::reference_internal
        )
        .def_prop_ro("cues", [](const cricodecs::wav::WavContainer& self) {
            return cue_list(self.cues());
        })
        .def_prop_ro("has_loops", [](const cricodecs::wav::WavContainer& self) {
            return self.has_loops();
        })
        .def_prop_ro("sample_count", [](const cricodecs::wav::WavContainer& self) {
            return self.sample_count();
        })
        .def_prop_ro("channels", [](const cricodecs::wav::WavContainer& self) {
            return self.channels();
        })
        .def_prop_ro("sample_rate", [](const cricodecs::wav::WavContainer& self) {
            return self.sample_rate();
        })
        .def("info", [](const cricodecs::wav::WavContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = path_or_none(self.source_path());
            info.attr("format") = self.format();
            info.attr("sampler") = self.sampler();
            info.attr("cues") = cue_list(self.cues());
            info.attr("has_loops") = self.has_loops();
            info.attr("sample_count") = self.sample_count();
            info.attr("channels") = self.channels();
            info.attr("sample_rate") = self.sample_rate();
            return info;
        })
        .def("pcm16le", [](const cricodecs::wav::WavContainer& self) {
            return pcm16_to_python_bytes(unwrap_expected(self.get_pcm16()));
        })
        .def("sample", [](const cricodecs::wav::WavContainer& self, size_t index) {
            return unwrap_expected(self.get_sample(index));
        }, nb::arg("index"));

    install_attr_repr(module, "GUID", {"data1", "data2", "data3", "data4"});
    install_attr_repr(module, "SampleLoop", {"cue_point_id", "type", "start", "end", "fraction", "play_count"});
    install_attr_repr(module, "CuePoint", {"name", "position", "chunk_id", "chunk_start", "block_start", "sample_offset"});
    install_attr_repr(module, "SamplerChunk", {"manufacturer", "product", "sample_period", "midi_unity_note", "midi_pitch_fraction", "smpte_format", "smpte_offset", "loops"});
    install_attr_repr(module, "WavFormat", {"compression_mode", "channels", "sample_rate", "avg_bytes_per_sec", "block_align", "bit_depth", "extension_size", "valid_bits_per_sample", "channel_mask", "sub_format"});
    install_attr_repr(module, "Wav", {"source_path", "format", "sampler", "cues", "has_loops", "sample_count", "channels", "sample_rate"});

    module.def(
        "build",
        [](const nb::object& output_path, const nb::bytes& pcm16le, uint32_t sample_rate, uint16_t channels, const nb::list& loops) {
            auto pcm = wav_pcm16_from_python_bytes(pcm16le);
            std::vector<cricodecs::wav::SampleLoop> native_loops;
            native_loops.reserve(static_cast<size_t>(PyList_Size(loops.ptr())));
            for (auto item : loops) {
                native_loops.push_back(nb::cast<cricodecs::wav::SampleLoop>(item));
            }
            unwrap_expected(cricodecs::wav::WavContainer::write(
                require_python_path(output_path, "output_path").string(), pcm, sample_rate, channels, native_loops));
        },
        nb::arg("output_path"),
        nb::arg("pcm16le"),
        nb::arg("sample_rate"),
        nb::arg("channels"),
        nb::arg("loops") = nb::list()
    );

    module.def(
        "build_bytes",
        [](const nb::bytes& pcm16le, uint32_t sample_rate, uint16_t channels, const nb::list& loops) {
            auto pcm = wav_pcm16_from_python_bytes(pcm16le);
            std::vector<cricodecs::wav::SampleLoop> native_loops;
            native_loops.reserve(static_cast<size_t>(PyList_Size(loops.ptr())));
            for (auto item : loops) {
                native_loops.push_back(nb::cast<cricodecs::wav::SampleLoop>(item));
            }
            return to_python_bytes(unwrap_expected(
                cricodecs::wav::WavContainer::build_bytes(pcm, sample_rate, channels, native_loops)));
        },
        nb::arg("pcm16le"),
        nb::arg("sample_rate"),
        nb::arg("channels"),
        nb::arg("loops") = nb::list()
    );

    module.def("load", &load_wav_any, nb::arg("source"));
}

} // namespace cricodecs::python
