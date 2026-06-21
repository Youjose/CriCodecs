#include "binding_helpers.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include <nanobind/stl/array.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/sfd/sfd_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::sfd::SfdStream& stream_at(const cricodecs::sfd::SfdContainer& self, uint32_t index) {
    const auto& streams = self.streams();
    if (index >= streams.size()) {
        raise_value_error("SFD stream index is out of range");
    }
    return streams[index];
}

[[nodiscard]] cricodecs::sfd::SfdContainer load_sfd_any(const nb::object& source) {
    return load_path_or_borrowed_source(source, [](const std::filesystem::path& path, std::span<const uint8_t> data) {
        if (!path.empty()) {
            return unwrap_expected(cricodecs::sfd::SfdContainer::load(path));
        }
        return unwrap_expected(cricodecs::sfd::SfdContainer::load(data));
    });
}

} // namespace

void bind_sfd_module(nb::module_& module) {
    nb::enum_<cricodecs::sfd::SfdStreamType>(module, "SfdStreamType")
        .value("AUDIO", cricodecs::sfd::SfdStreamType::audio)
        .value("VIDEO", cricodecs::sfd::SfdStreamType::video)
        .value("PRIVATE_DATA", cricodecs::sfd::SfdStreamType::private_data);

    nb::enum_<cricodecs::sfd::SfdAudioType>(module, "SfdAudioType")
        .value("UNKNOWN", cricodecs::sfd::SfdAudioType::unknown)
        .value("ADX", cricodecs::sfd::SfdAudioType::adx)
        .value("AIX", cricodecs::sfd::SfdAudioType::aix)
        .value("AC3", cricodecs::sfd::SfdAudioType::ac3);

    nb::enum_<cricodecs::sfd::SfdVideoType>(module, "SfdVideoType")
        .value("UNKNOWN", cricodecs::sfd::SfdVideoType::unknown)
        .value("MPEG1", cricodecs::sfd::SfdVideoType::mpeg1)
        .value("MPEG2", cricodecs::sfd::SfdVideoType::mpeg2);

    nb::enum_<cricodecs::sfd::SfdHeaderVariant>(module, "SfdHeaderVariant")
        .value("UNKNOWN", cricodecs::sfd::SfdHeaderVariant::unknown)
        .value("SOFDEC_STREAM", cricodecs::sfd::SfdHeaderVariant::sofdec_stream)
        .value("SOFDEC_STREAM2", cricodecs::sfd::SfdHeaderVariant::sofdec_stream2);

    nb::enum_<cricodecs::sfd::SfdBuildProfile>(module, "SfdBuildProfile")
        .value(
            "SOFDEC_STREAM_STANDARD_FIXED_2048",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream_standard_fixed_2048
        )
        .value(
            "SOFDEC_STREAM_FIXED_2048",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream_fixed_2048
        )
        .value(
            "SOFDEC_STREAM_SFDMUXG_FIXED_2048",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream_sfdmuxg_fixed_2048
        )
        .value(
            "SOFDEC_STREAM2_FIXED_2048_V23249",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23249
        )
        .value(
            "SOFDEC_STREAM2_FIXED_2048_V23310",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23310
        )
        .value(
            "SOFDEC_STREAM2_CRAFT",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream2_craft
        )
        .value(
            "SOFDEC_STREAM2_MEDIANOCHE",
            cricodecs::sfd::SfdBuildProfile::sofdec_stream2_medianoche
        );

    nb::class_<cricodecs::sfd::SfdVideoSequenceHeader>(module, "SfdVideoSequenceHeader")
        .def_ro("width", &cricodecs::sfd::SfdVideoSequenceHeader::width)
        .def_ro("height", &cricodecs::sfd::SfdVideoSequenceHeader::height)
        .def_ro("aspect_ratio_code", &cricodecs::sfd::SfdVideoSequenceHeader::aspect_ratio_code)
        .def_ro("frame_rate_code", &cricodecs::sfd::SfdVideoSequenceHeader::frame_rate_code)
        .def_ro("bit_rate_value", &cricodecs::sfd::SfdVideoSequenceHeader::bit_rate_value);

    nb::class_<cricodecs::sfd::SfdChunkSpan>(module, "SfdChunkSpan")
        .def_ro("source_offset", &cricodecs::sfd::SfdChunkSpan::source_offset)
        .def_ro("size", &cricodecs::sfd::SfdChunkSpan::size);

    nb::class_<cricodecs::sfd::SfdElementRecord>(module, "SfdElementRecord")
        .def_ro("stream_id", &cricodecs::sfd::SfdElementRecord::stream_id)
        .def_ro("source_type", &cricodecs::sfd::SfdElementRecord::source_type)
        .def_ro("short_name", &cricodecs::sfd::SfdElementRecord::short_name)
        .def_ro("timestamp", &cricodecs::sfd::SfdElementRecord::timestamp)
        .def_prop_ro("detail_bytes", [](const cricodecs::sfd::SfdElementRecord& self) {
            return to_python_bytes(self.detail_bytes);
        })
        .def_prop_ro("footer_bytes", [](const cricodecs::sfd::SfdElementRecord& self) {
            return to_python_bytes(self.footer_bytes);
        })
        .def_ro("picture_rate", &cricodecs::sfd::SfdElementRecord::picture_rate)
        .def_ro("width", &cricodecs::sfd::SfdElementRecord::width)
        .def_ro("height", &cricodecs::sfd::SfdElementRecord::height)
        .def_ro("frame_rate_code", &cricodecs::sfd::SfdElementRecord::frame_rate_code)
        .def_ro("audio_channels", &cricodecs::sfd::SfdElementRecord::audio_channels)
        .def_ro("audio_sample_rate", &cricodecs::sfd::SfdElementRecord::audio_sample_rate);

    nb::class_<cricodecs::sfd::SfdHeaderSummary>(module, "SfdHeaderSummary")
        .def_ro("variant", &cricodecs::sfd::SfdHeaderSummary::variant)
        .def_ro("header_label", &cricodecs::sfd::SfdHeaderSummary::header_label)
        .def_prop_ro("version_tag_bytes", [](const cricodecs::sfd::SfdHeaderSummary& self) {
            return to_python_bytes(self.version_tag_bytes);
        })
        .def_ro("version_tag_size", &cricodecs::sfd::SfdHeaderSummary::version_tag_size)
        .def_ro("pack_size", &cricodecs::sfd::SfdHeaderSummary::pack_size)
        .def_ro("variable_pack", &cricodecs::sfd::SfdHeaderSummary::variable_pack)
        .def_ro("min_header_packet_count", &cricodecs::sfd::SfdHeaderSummary::min_header_packet_count)
        .def_ro("reserved_header_size", &cricodecs::sfd::SfdHeaderSummary::reserved_header_size)
        .def_ro("element_count", &cricodecs::sfd::SfdHeaderSummary::element_count)
        .def_ro("audio_count", &cricodecs::sfd::SfdHeaderSummary::audio_count)
        .def_ro("video_count", &cricodecs::sfd::SfdHeaderSummary::video_count)
        .def_ro("private_count", &cricodecs::sfd::SfdHeaderSummary::private_count)
        .def_ro("bitrate_bytes_per_second", &cricodecs::sfd::SfdHeaderSummary::bitrate_bytes_per_second)
        .def_ro("short_output_name", &cricodecs::sfd::SfdHeaderSummary::short_output_name)
        .def_ro("output_timestamp", &cricodecs::sfd::SfdHeaderSummary::output_timestamp)
        .def_ro("output_name", &cricodecs::sfd::SfdHeaderSummary::output_name)
        .def_ro("builder_version", &cricodecs::sfd::SfdHeaderSummary::builder_version)
        .def_ro("element_records", &cricodecs::sfd::SfdHeaderSummary::element_records);

    nb::class_<cricodecs::sfd::SfdStream>(module, "SfdStream")
        .def_ro("index", &cricodecs::sfd::SfdStream::index)
        .def_ro("type_index", &cricodecs::sfd::SfdStream::type_index)
        .def_ro("type", &cricodecs::sfd::SfdStream::type)
        .def_ro("stream_id", &cricodecs::sfd::SfdStream::stream_id)
        .def_ro("audio_type", &cricodecs::sfd::SfdStream::audio_type)
        .def_ro("video_type", &cricodecs::sfd::SfdStream::video_type)
        .def_ro("source_name", &cricodecs::sfd::SfdStream::source_name)
        .def_ro("video_header", &cricodecs::sfd::SfdStream::video_header)
        .def_ro("element_record", &cricodecs::sfd::SfdStream::element_record)
        .def_ro("extracted_size", &cricodecs::sfd::SfdStream::extracted_size)
        .def_ro("packet_count", &cricodecs::sfd::SfdStream::packet_count)
        .def_ro("chunks", &cricodecs::sfd::SfdStream::chunks)
        .def(
            "suggested_path",
            [](const cricodecs::sfd::SfdStream& self, bool include_index_prefix) {
                return self.suggested_path(include_index_prefix).generic_string();
            },
            nb::arg("include_index_prefix") = true
        );

    nb::class_<cricodecs::sfd::SfdContainer>(module, "Sfd")
        .def_static("load", [](const std::string& path) {
            return unwrap_expected(cricodecs::sfd::SfdContainer::load(std::filesystem::path(path)));
        }, nb::arg("path"))
        .def_static("load_bytes", [](const nb::bytes& data) {
            auto owned = copy_python_bytes(data);
            return unwrap_expected(cricodecs::sfd::SfdContainer::load(std::move(owned)));
        }, nb::arg("data"))
        .def_prop_ro("source_path", [](const cricodecs::sfd::SfdContainer& self) {
            return self.source_path().generic_string();
        })
        .def_prop_ro("stream_count", &cricodecs::sfd::SfdContainer::stream_count)
        .def_prop_ro("streams", [](const cricodecs::sfd::SfdContainer& self) {
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            return streams;
        })
        .def_prop_ro("header_summary", [](const cricodecs::sfd::SfdContainer& self) -> nb::object {
            if (!self.header_summary().has_value()) {
                return nb::none();
            }
            return nb::cast(*self.header_summary());
        })
        .def("info", [](const cricodecs::sfd::SfdContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("stream_count") = self.stream_count();
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            info.attr("streams") = streams;
            info.attr("header_summary") = self.header_summary().has_value() ? nb::cast(*self.header_summary()) : nb::none();
            return info;
        })
        .def("stream", [](const cricodecs::sfd::SfdContainer& self, uint32_t index) {
            return stream_at(self, index);
        }, nb::arg("index"))
        .def("find_stream_by_id", [](const cricodecs::sfd::SfdContainer& self, uint8_t stream_id) -> nb::object {
            const auto* stream = self.find_stream_by_id(stream_id);
            if (stream == nullptr) {
                return nb::none();
            }
            return nb::cast(*stream);
        }, nb::arg("stream_id"))
        .def("stream_bytes", [](const cricodecs::sfd::SfdContainer& self, uint32_t index) {
            return to_python_bytes(unwrap_expected(self.extract_stream(stream_at(self, index).index)));
        }, nb::arg("index"))
        .def("extract_file", [](const cricodecs::sfd::SfdContainer& self, uint32_t index, const nb::object& output_path) {
            unwrap_expected(self.extract_file(stream_at(self, index).index, require_python_path(output_path, "output_path")));
        }, nb::arg("index"), nb::arg("output_path"))
        .def("demux", [](const cricodecs::sfd::SfdContainer& self, bool include_index_prefix) {
            auto streams = unwrap_expected(self.demux(include_index_prefix));
            nb::dict out;
            for (auto& [name, bytes] : streams) {
                out[nb::str(name.c_str())] = to_python_bytes(bytes);
            }
            return out;
        }, nb::arg("include_index_prefix") = true)
        .def("extract", [](const cricodecs::sfd::SfdContainer& self, const std::string& output_dir) {
            unwrap_expected(self.extract(std::filesystem::path(output_dir)));
        }, nb::arg("output_dir"))
        .def("save", [](const cricodecs::sfd::SfdContainer& self) {
            return to_python_bytes(unwrap_expected(self.save()));
        })
        .def("save", [](const cricodecs::sfd::SfdContainer& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"))
        .def("save_to_file", [](const cricodecs::sfd::SfdContainer& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"));

    install_attr_repr(module, "SfdVideoSequenceHeader", {"width", "height", "aspect_ratio_code", "frame_rate_code", "bit_rate_value"});
    install_attr_repr(module, "SfdChunkSpan", {"source_offset", "size"});
    install_attr_repr(module, "SfdElementRecord", {"stream_id", "source_type", "short_name", "timestamp", "picture_rate", "width", "height", "frame_rate_code", "audio_channels", "audio_sample_rate"});
    install_attr_repr(module, "SfdHeaderSummary", {"variant", "header_label", "version_tag_size", "pack_size", "variable_pack", "min_header_packet_count", "reserved_header_size", "element_count", "audio_count", "video_count", "private_count", "bitrate_bytes_per_second", "short_output_name", "output_timestamp", "output_name", "builder_version", "element_records"});
    install_attr_repr(module, "SfdStream", {"index", "type_index", "type", "stream_id", "audio_type", "video_type", "source_name", "video_header", "element_record", "extracted_size", "packet_count", "chunks"});
    install_attr_repr(module, "Sfd", {"source_path", "stream_count", "streams", "header_summary"});

    module.def(
        "mux",
        [](
            const std::string& video_path,
            const std::optional<std::string>& audio_path,
            const std::string& video_source_name,
            const std::string& audio_source_name,
            const std::string& video_stream_name,
            const std::string& audio_stream_name,
            const std::string& output_name,
            const std::optional<cricodecs::sfd::SfdBuildProfile>& build_profile,
            const std::string& header_builder_version,
            const std::optional<cricodecs::sfd::SfdBuildProfile>& mux_profile,
            const std::string& header_builder_version_override
        ) {
            cricodecs::sfd::SfdBuildInput input;
            input.video_path = std::filesystem::path(video_path);
            if (audio_path.has_value()) {
                input.audio_path = std::filesystem::path(*audio_path);
            }
            input.video_source_name = video_source_name;
            input.audio_source_name = audio_source_name;
            input.video_stream_name = video_stream_name;
            input.audio_stream_name = audio_stream_name;
            input.output_name = output_name;
            input.build_profile = build_profile;
            input.header_builder_version = header_builder_version;
            input.mux_profile = mux_profile;
            input.header_builder_version_override = header_builder_version_override;

            cricodecs::sfd::SfdBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("video_path"),
        nb::arg("audio_path") = nb::none(),
        nb::arg("video_source_name") = "",
        nb::arg("audio_source_name") = "",
        nb::arg("video_stream_name") = "",
        nb::arg("audio_stream_name") = "",
        nb::arg("output_name") = "",
        nb::arg("build_profile") = nb::none(),
        nb::arg("header_builder_version") = "",
        nb::arg("mux_profile") = nb::none(),
        nb::arg("header_builder_version_override") = ""
    );
    module.def(
        "mux_to_file",
        [](
            const std::string& output_path,
            const std::string& video_path,
            const std::optional<std::string>& audio_path,
            const std::string& video_source_name,
            const std::string& audio_source_name,
            const std::string& video_stream_name,
            const std::string& audio_stream_name,
            const std::string& output_name,
            const std::optional<cricodecs::sfd::SfdBuildProfile>& build_profile,
            const std::string& header_builder_version,
            const std::optional<cricodecs::sfd::SfdBuildProfile>& mux_profile,
            const std::string& header_builder_version_override
        ) {
            cricodecs::sfd::SfdBuildInput input;
            input.video_path = std::filesystem::path(video_path);
            if (audio_path.has_value()) {
                input.audio_path = std::filesystem::path(*audio_path);
            }
            input.video_source_name = video_source_name;
            input.audio_source_name = audio_source_name;
            input.video_stream_name = video_stream_name;
            input.audio_stream_name = audio_stream_name;
            input.output_name = output_name;
            input.build_profile = build_profile;
            input.header_builder_version = header_builder_version;
            input.mux_profile = mux_profile;
            input.header_builder_version_override = header_builder_version_override;

            cricodecs::sfd::SfdBuilder builder;
            unwrap_expected(builder.build_to_file(std::filesystem::path(output_path), input));
        },
        nb::arg("output_path"),
        nb::arg("video_path"),
        nb::arg("audio_path") = nb::none(),
        nb::arg("video_source_name") = "",
        nb::arg("audio_source_name") = "",
        nb::arg("video_stream_name") = "",
        nb::arg("audio_stream_name") = "",
        nb::arg("output_name") = "",
        nb::arg("build_profile") = nb::none(),
        nb::arg("header_builder_version") = "",
        nb::arg("mux_profile") = nb::none(),
        nb::arg("header_builder_version_override") = ""
    );
    module.def("load", &load_sfd_any, nb::arg("source"));
    module.def("demux", [](const nb::object& source, bool include_index_prefix) {
        auto sfd = load_sfd_any(source);
        auto streams = unwrap_expected(sfd.demux(include_index_prefix));
        nb::dict out;
        for (auto& [name, bytes] : streams) {
            out[nb::str(name.c_str())] = to_python_bytes(bytes);
        }
        return out;
    }, nb::arg("source"), nb::arg("include_index_prefix") = true);
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto sfd = load_sfd_any(source);
        unwrap_expected(sfd.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
