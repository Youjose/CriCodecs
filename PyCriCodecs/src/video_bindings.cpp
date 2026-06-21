#include "binding_helpers.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <nanobind/stl/pair.h>

#include "../../CriCodecs/src/video/h264.hpp"
#include "../../CriCodecs/src/video/ivf.hpp"
#include "../../CriCodecs/src/video/mpeg.hpp"

namespace cricodecs::python {
namespace {

template <typename Reader>
[[nodiscard]] Reader load_reader(const std::string& path) {
    Reader reader;
    unwrap_expected(reader.open(std::filesystem::path(path)));
    return reader;
}

[[nodiscard]] nb::object ivf_next_frame(cricodecs::video::IvfReader& reader) {
    if (!reader.has_frames()) {
        return nb::none();
    }
    auto frame = unwrap_expected(reader.read_next_frame());
    nb::dict out;
    out["size"] = frame.size;
    out["timestamp"] = frame.timestamp;
    out["is_keyframe"] = frame.is_keyframe;
    out["data"] = to_python_bytes(frame.data);
    out["record_bytes"] = to_python_bytes(frame.record_bytes);
    return out;
}

[[nodiscard]] nb::object mpeg_next_frame(cricodecs::video::MpegVideoReader& reader) {
    if (!reader.has_frames()) {
        return nb::none();
    }
    auto frame = unwrap_expected(reader.read_next_frame());
    nb::dict out;
    out["size"] = frame.size;
    out["index"] = frame.index;
    out["is_keyframe"] = frame.is_keyframe;
    out["data"] = to_python_bytes(frame.data);
    out["record_bytes"] = to_python_bytes(frame.record_bytes);
    return out;
}

[[nodiscard]] nb::object h264_next_frame(cricodecs::video::H264VideoReader& reader) {
    if (!reader.has_frames()) {
        return nb::none();
    }
    auto frame = unwrap_expected(reader.read_next_frame());
    nb::dict out;
    out["size"] = frame.size;
    out["index"] = frame.index;
    out["is_keyframe"] = frame.is_keyframe;
    out["data"] = to_python_bytes(frame.data);
    out["record_bytes"] = to_python_bytes(frame.record_bytes);
    return out;
}

[[nodiscard]] nb::object load_video_any(const nb::object& source) {
    auto path = python_text_path(source);
    if (!path) {
        raise_type_error("video.load source must be a filesystem path");
    }

    std::array<uint8_t, 8> prefix{};
    std::ifstream input(*path, std::ios::binary);
    if (!input) {
        raise_value_error("video load failed: could not open input file");
    }
    input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    const auto size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    const auto starts_with = [&](std::string_view magic) {
        return size >= magic.size() &&
            std::equal(magic.begin(), magic.end(), prefix.begin(), [](char lhs, uint8_t rhs) {
                return static_cast<uint8_t>(lhs) == rhs;
            });
    };

    if (starts_with("DKIF")) {
        return nb::cast(load_reader<cricodecs::video::IvfReader>(*path));
    }
    if (starts_with(std::string_view("\x00\x00\x00\x01\x09", 5)) ||
        starts_with(std::string_view("\x00\x00\x00\x01\x67", 5)) ||
        starts_with(std::string_view("\x00\x00\x01\x09", 4)) ||
        starts_with(std::string_view("\x00\x00\x01\x67", 4))) {
        return nb::cast(load_reader<cricodecs::video::H264VideoReader>(*path));
    }
    return nb::cast(load_reader<cricodecs::video::MpegVideoReader>(*path));
}

} // namespace

void bind_video_module(nb::module_& module) {
    nb::enum_<cricodecs::video::MpegVideoType>(module, "MpegVideoType")
        .value("UNKNOWN", cricodecs::video::MpegVideoType::unknown)
        .value("MPEG1", cricodecs::video::MpegVideoType::mpeg1)
        .value("MPEG2", cricodecs::video::MpegVideoType::mpeg2);

    nb::class_<cricodecs::video::IvfHeader>(module, "IvfHeader")
        .def_ro("magic", &cricodecs::video::IvfHeader::magic)
        .def_ro("version", &cricodecs::video::IvfHeader::version)
        .def_ro("header_size", &cricodecs::video::IvfHeader::header_size)
        .def_ro("fourcc", &cricodecs::video::IvfHeader::fourcc)
        .def_ro("width", &cricodecs::video::IvfHeader::width)
        .def_ro("height", &cricodecs::video::IvfHeader::height)
        .def_ro("rate", &cricodecs::video::IvfHeader::rate)
        .def_ro("scale", &cricodecs::video::IvfHeader::scale)
        .def_ro("num_frames", &cricodecs::video::IvfHeader::num_frames)
        .def_ro("unused", &cricodecs::video::IvfHeader::unused);

    nb::class_<cricodecs::video::MpegVideoSequenceHeader>(module, "MpegVideoSequenceHeader")
        .def_ro("width", &cricodecs::video::MpegVideoSequenceHeader::width)
        .def_ro("height", &cricodecs::video::MpegVideoSequenceHeader::height)
        .def_ro("aspect_ratio_code", &cricodecs::video::MpegVideoSequenceHeader::aspect_ratio_code)
        .def_ro("frame_rate_code", &cricodecs::video::MpegVideoSequenceHeader::frame_rate_code)
        .def_ro("bit_rate_value", &cricodecs::video::MpegVideoSequenceHeader::bit_rate_value);

    nb::class_<cricodecs::video::H264SequenceParameterSet>(module, "H264SequenceParameterSet")
        .def_ro("width", &cricodecs::video::H264SequenceParameterSet::width)
        .def_ro("height", &cricodecs::video::H264SequenceParameterSet::height)
        .def_ro("profile_idc", &cricodecs::video::H264SequenceParameterSet::profile_idc)
        .def_ro("level_idc", &cricodecs::video::H264SequenceParameterSet::level_idc)
        .def_ro("num_units_in_tick", &cricodecs::video::H264SequenceParameterSet::num_units_in_tick)
        .def_ro("time_scale", &cricodecs::video::H264SequenceParameterSet::time_scale)
        .def_ro("fixed_frame_rate", &cricodecs::video::H264SequenceParameterSet::fixed_frame_rate);

    nb::class_<cricodecs::video::IvfReader>(module, "IvfReader")
        .def(nb::init<>())
        .def_static("load", &load_reader<cricodecs::video::IvfReader>, nb::arg("path"))
        .def_prop_ro("header", &cricodecs::video::IvfReader::get_header)
        .def_prop_ro("raw_header", [](const cricodecs::video::IvfReader& reader) {
            return to_python_bytes(reader.get_raw_header());
        })
        .def_prop_ro("has_frames", &cricodecs::video::IvfReader::has_frames)
        .def("read_next_frame", &ivf_next_frame);

    nb::class_<cricodecs::video::MpegVideoReader>(module, "MpegVideoReader")
        .def(nb::init<>())
        .def_static("load", &load_reader<cricodecs::video::MpegVideoReader>, nb::arg("path"))
        .def_prop_ro("sequence_header", &cricodecs::video::MpegVideoReader::sequence_header)
        .def_prop_ro("video_type", &cricodecs::video::MpegVideoReader::video_type)
        .def_prop_ro("frame_rate", &cricodecs::video::MpegVideoReader::frame_rate)
        .def_prop_ro("frame_count", &cricodecs::video::MpegVideoReader::frame_count)
        .def_prop_ro("has_frames", &cricodecs::video::MpegVideoReader::has_frames)
        .def("read_next_frame", &mpeg_next_frame);

    nb::class_<cricodecs::video::H264VideoReader>(module, "H264VideoReader")
        .def(nb::init<>())
        .def_static("load", &load_reader<cricodecs::video::H264VideoReader>, nb::arg("path"))
        .def_prop_ro("sequence_parameter_set", &cricodecs::video::H264VideoReader::sequence_parameter_set)
        .def_prop_ro("frame_rate", &cricodecs::video::H264VideoReader::frame_rate)
        .def_prop_ro("frame_count", &cricodecs::video::H264VideoReader::frame_count)
        .def_prop_ro("has_frames", &cricodecs::video::H264VideoReader::has_frames)
        .def("read_next_frame", &h264_next_frame);

    module.attr("Ivf") = module.attr("IvfReader");
    module.attr("Mpeg") = module.attr("MpegVideoReader");
    module.attr("H264") = module.attr("H264VideoReader");

    install_attr_repr(module, "IvfHeader", {"magic", "version", "header_size", "fourcc", "width", "height", "rate", "scale", "num_frames", "unused"});
    install_attr_repr(module, "MpegVideoSequenceHeader", {"width", "height", "aspect_ratio_code", "frame_rate_code", "bit_rate_value"});
    install_attr_repr(module, "H264SequenceParameterSet", {"width", "height", "profile_idc", "level_idc", "num_units_in_tick", "time_scale", "fixed_frame_rate"});
    install_attr_repr(module, "IvfReader", {"header", "has_frames"});
    install_attr_repr(module, "MpegVideoReader", {"sequence_header", "video_type", "frame_rate", "frame_count", "has_frames"});
    install_attr_repr(module, "H264VideoReader", {"sequence_parameter_set", "frame_rate", "frame_count", "has_frames"});

    module.def("load", &load_video_any, nb::arg("source"));
}

} // namespace cricodecs::python
