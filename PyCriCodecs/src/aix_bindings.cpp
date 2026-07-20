#include "binding_helpers.hpp"

#include <filesystem>

#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/aix/aix_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] nb::list segments_list(const cricodecs::aix::Aix& aix) {
    nb::list segments;
    for (const auto& segment : aix.segments()) {
        segments.append(segment);
    }
    return segments;
}

[[nodiscard]] nb::list layers_list(const cricodecs::aix::Aix& aix) {
    nb::list layers;
    for (const auto& layer : aix.layers()) {
        layers.append(layer);
    }
    return layers;
}

[[nodiscard]] cricodecs::aix::Aix load_aix_any(const nb::object& source) {
    cricodecs::aix::Aix aix;
    if (auto path = python_text_path(source)) {
        unwrap_expected(aix.load(std::filesystem::path(*path)));
        return aix;
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    unwrap_expected(aix.load(std::move(owned)));
    return aix;
}

[[nodiscard]] std::vector<std::vector<uint8_t>> copy_aix_payloads(
    const nb::object& source,
    const char* argument_name
) {
    if (!PySequence_Check(source.ptr())) {
        raise_type_error(std::string(argument_name) + " must be a sequence of bytes-like ADX payloads");
    }
    std::vector<std::vector<uint8_t>> payloads;
    const auto count = static_cast<size_t>(PySequence_Size(source.ptr()));
    payloads.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        auto item = nb::steal<nb::object>(PySequence_GetItem(source.ptr(), static_cast<Py_ssize_t>(index)));
        auto borrowed = borrow_python_source(item);
        const auto data = borrowed.as_span();
        payloads.emplace_back(data.begin(), data.end());
    }
    return payloads;
}

} // namespace

void bind_aix_module(nb::module_& module) {
    nb::class_<cricodecs::aix::AixSegment>(module, "AixSegment")
        .def_ro("offset", &cricodecs::aix::AixSegment::offset)
        .def_ro("size", &cricodecs::aix::AixSegment::size)
        .def_ro("sample_count", &cricodecs::aix::AixSegment::sample_count)
        .def_ro("sample_rate", &cricodecs::aix::AixSegment::sample_rate);

    nb::class_<cricodecs::aix::AixLayer>(module, "AixLayer")
        .def_ro("sample_rate", &cricodecs::aix::AixLayer::sample_rate)
        .def_ro("channel_count", &cricodecs::aix::AixLayer::channel_count);

    nb::class_<cricodecs::aix::AixLoopInfo>(module, "AixLoopInfo")
        .def_ro("start_segment", &cricodecs::aix::AixLoopInfo::start_segment)
        .def_ro("end_segment", &cricodecs::aix::AixLoopInfo::end_segment)
        .def_ro("start_sample", &cricodecs::aix::AixLoopInfo::start_sample)
        .def_ro("end_sample", &cricodecs::aix::AixLoopInfo::end_sample);

    nb::class_<cricodecs::aix::AixBuildSegment>(module, "AixBuildSegment")
        .def(nb::init<>())
        .def(
            "__init__",
            [](cricodecs::aix::AixBuildSegment* self, const nb::object& layers) {
                cricodecs::aix::AixBuildSegment segment;
                if (!PySequence_Check(layers.ptr())) {
                    raise_type_error("AixBuildSegment layer_adx_data must be a sequence");
                }
                const auto count = static_cast<size_t>(PySequence_Size(layers.ptr()));
                segment.layer_adx_data.reserve(count);
                for (size_t index = 0; index < count; ++index) {
                    auto item = nb::steal<nb::object>(PySequence_GetItem(layers.ptr(), static_cast<Py_ssize_t>(index)));
                    auto borrowed = borrow_python_source(item);
                    const auto data = borrowed.as_span();
                    segment.layer_adx_data.emplace_back(data.begin(), data.end());
                }
                new (self) cricodecs::aix::AixBuildSegment(std::move(segment));
            },
            nb::arg("layer_adx_data")
        )
        .def_rw("layer_adx_data", &cricodecs::aix::AixBuildSegment::layer_adx_data);

    nb::class_<cricodecs::aix::Aix>(module, "Aix")
        .def_static("load", &load_aix_any, nb::arg("source"))
        .def_static("load_bytes", [](const nb::bytes& data) {
            cricodecs::aix::Aix aix;
            auto owned = copy_python_bytes(data);
            unwrap_expected(aix.load(std::move(owned)));
            return aix;
        }, nb::arg("data"))
        .def_prop_ro("source_path", [](const cricodecs::aix::Aix& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("segments", [](const cricodecs::aix::Aix& self) {
            return segments_list(self);
        })
        .def_prop_ro("layers", [](const cricodecs::aix::Aix& self) {
            return layers_list(self);
        })
        .def_prop_ro("total_sample_count", &cricodecs::aix::Aix::total_sample_count)
        .def("info", [](const cricodecs::aix::Aix& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("segments") = segments_list(self);
            info.attr("layers") = layers_list(self);
            info.attr("total_sample_count") = self.total_sample_count();
            info.attr("inferred_loop") = self.inferred_loop().has_value() ? nb::cast(*self.inferred_loop()) : nb::none();
            return info;
        })
        .def_prop_ro("inferred_loop", [](const cricodecs::aix::Aix& self) -> nb::object {
            if (!self.inferred_loop().has_value()) {
                return nb::none();
            }
            return nb::cast(*self.inferred_loop());
        })
        .def("segment_bytes", [](const cricodecs::aix::Aix& self, size_t segment_index, size_t layer_index) {
            auto bytes = unwrap_expected(self.segment_bytes(segment_index, layer_index));
            return to_python_bytes(bytes);
        }, nb::arg("segment_index"), nb::arg("layer_index"))
        .def("extract_file", [](const cricodecs::aix::Aix& self, size_t segment_index, size_t layer_index, const nb::object& output_path) {
            unwrap_expected(self.extract_file(segment_index, layer_index, require_python_path(output_path, "output_path")));
        }, nb::arg("segment_index"), nb::arg("layer_index"), nb::arg("output_path"))
        .def("extract", [](const cricodecs::aix::Aix& self, const std::string& output_dir) {
            unwrap_expected(self.extract(output_dir));
        }, nb::arg("output_dir"))
        .def("save", [](const cricodecs::aix::Aix& self) {
            return to_python_bytes(unwrap_expected(self.save()));
        })
        .def("save_to_file", [](const cricodecs::aix::Aix& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"))
        .def("add_segment", [](cricodecs::aix::Aix& self, cricodecs::aix::AixBuildSegment segment) {
            unwrap_expected(self.add_segment(std::move(segment)));
        }, nb::arg("segment"))
        .def("replace_segment", [](cricodecs::aix::Aix& self, size_t index, cricodecs::aix::AixBuildSegment segment) {
            unwrap_expected(self.replace_segment(index, std::move(segment)));
        }, nb::arg("index"), nb::arg("segment"))
        .def("remove_segment", [](cricodecs::aix::Aix& self, size_t index) {
            unwrap_expected(self.remove_segment(index));
        }, nb::arg("index"))
        .def("move_segment", [](cricodecs::aix::Aix& self, size_t from_index, size_t to_index) {
            unwrap_expected(self.move_segment(from_index, to_index));
        }, nb::arg("from_index"), nb::arg("to_index"))
        .def("add_layer", [](cricodecs::aix::Aix& self, const nb::object& segment_adx_data) {
            unwrap_expected(self.add_layer(copy_aix_payloads(segment_adx_data, "segment_adx_data")));
        }, nb::arg("segment_adx_data"))
        .def("replace_layer", [](cricodecs::aix::Aix& self, size_t segment_index, size_t layer_index, const nb::object& data) {
            auto borrowed = borrow_python_source(data);
            unwrap_expected(self.replace_layer(segment_index, layer_index, borrowed.as_span()));
        }, nb::arg("segment_index"), nb::arg("layer_index"), nb::arg("data"))
        .def("remove_layer", [](cricodecs::aix::Aix& self, size_t index) {
            unwrap_expected(self.remove_layer(index));
        }, nb::arg("index"))
        .def("move_layer", [](cricodecs::aix::Aix& self, size_t from_index, size_t to_index) {
            unwrap_expected(self.move_layer(from_index, to_index));
        }, nb::arg("from_index"), nb::arg("to_index"));

    install_attr_repr(module, "AixSegment", {"offset", "size", "sample_count", "sample_rate"});
    install_attr_repr(module, "AixLayer", {"sample_rate", "channel_count"});
    install_attr_repr(module, "AixLoopInfo", {"start_segment", "end_segment", "start_sample", "end_sample"});
    install_attr_repr(module, "AixBuildSegment", {"layer_adx_data"});
    install_attr_repr(module, "Aix", {"source_path", "segments", "layers", "total_sample_count", "inferred_loop"});

    module.def("build",
        [](const std::vector<cricodecs::aix::AixBuildSegment>& segments) {
            return to_python_bytes(unwrap_expected(cricodecs::aix::Aix::build(segments)));
        },
        nb::arg("segments")
    );
    module.def("build",
        [](const std::vector<cricodecs::aix::AixBuildSegment>& segments, const nb::object& output_path) {
            unwrap_expected(cricodecs::aix::Aix::build_to_file(segments, require_python_path(output_path, "output_path")));
        },
        nb::arg("segments"),
        nb::arg("output_path")
    );

    module.def("build",
        [](const nb::list& segments) {
            std::vector<cricodecs::aix::AixBuildSegment> native_segments;
            native_segments.reserve(static_cast<size_t>(PyList_Size(segments.ptr())));
            for (size_t segment_index = 0; segment_index < static_cast<size_t>(PyList_Size(segments.ptr())); ++segment_index) {
                auto item = segments[segment_index];
                auto layers_object = nb::getattr(item, "layer_adx_data");
                if (!PySequence_Check(layers_object.ptr())) {
                    raise_value_error("AIX build failed: each segment.layer_adx_data must be a sequence of bytes-like ADX payloads");
                }

                cricodecs::aix::AixBuildSegment native_segment;
                const auto layer_count = static_cast<size_t>(PySequence_Size(layers_object.ptr()));
                native_segment.layer_adx_data.reserve(layer_count);
                for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
                    auto layer_item = nb::steal<nb::object>(
                        PySequence_GetItem(layers_object.ptr(), static_cast<Py_ssize_t>(layer_index))
                    );
                    if (!PyBytes_Check(layer_item.ptr())) {
                        raise_value_error(
                            "AIX build failed: each segment.layer_adx_data entry must already be bytes");
                    }

                    const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(layer_item));
                    native_segment.layer_adx_data.emplace_back(
                        reinterpret_cast<const uint8_t*>(data_view.data()),
                        reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                    );
                }
                native_segments.push_back(std::move(native_segment));
            }

            return to_python_bytes(unwrap_expected(cricodecs::aix::Aix::build(native_segments)));
        },
        nb::arg("segments")
    );
    module.def("build_to_file",
        [](const nb::list& segments, const std::string& output_path) {
            std::vector<cricodecs::aix::AixBuildSegment> native_segments;
            native_segments.reserve(static_cast<size_t>(PyList_Size(segments.ptr())));
            for (size_t segment_index = 0; segment_index < static_cast<size_t>(PyList_Size(segments.ptr())); ++segment_index) {
                auto item = segments[segment_index];
                auto layers_object = nb::getattr(item, "layer_adx_data");
                if (!PySequence_Check(layers_object.ptr())) {
                    raise_value_error("AIX build failed: each segment.layer_adx_data must be a sequence of bytes-like ADX payloads");
                }

                cricodecs::aix::AixBuildSegment native_segment;
                const auto layer_count = static_cast<size_t>(PySequence_Size(layers_object.ptr()));
                native_segment.layer_adx_data.reserve(layer_count);
                for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
                    auto layer_item = nb::steal<nb::object>(
                        PySequence_GetItem(layers_object.ptr(), static_cast<Py_ssize_t>(layer_index))
                    );
                    if (!PyBytes_Check(layer_item.ptr())) {
                        raise_value_error(
                            "AIX build failed: each segment.layer_adx_data entry must already be bytes");
                    }

                    const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(layer_item));
                    native_segment.layer_adx_data.emplace_back(
                        reinterpret_cast<const uint8_t*>(data_view.data()),
                        reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                    );
                }
                native_segments.push_back(std::move(native_segment));
            }

            unwrap_expected(cricodecs::aix::Aix::build_to_file(
                native_segments,
                std::filesystem::path(output_path)
            ));
        },
        nb::arg("segments"),
        nb::arg("output_path")
    );
    module.def("load", &load_aix_any, nb::arg("source"));
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto aix = load_aix_any(source);
        unwrap_expected(aix.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
