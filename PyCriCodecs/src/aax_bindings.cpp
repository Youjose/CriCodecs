#include "binding_helpers.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include "../../CriCodecs/src/aax/aax_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] nb::list segment_list(const cricodecs::aax::AaxContainer& aax) {
    nb::list segments;
    for (const auto& segment : aax.segments()) {
        segments.append(segment);
    }
    return segments;
}

[[nodiscard]] cricodecs::aax::AaxContainer load_aax_any(const nb::object& source) {
    return load_path_or_borrowed_source(source, [](const std::filesystem::path& path, std::span<const uint8_t> data) {
        if (!path.empty()) {
            return unwrap_expected(cricodecs::aax::AaxContainer::load(path));
        }
        return unwrap_expected(cricodecs::aax::AaxContainer::load(data));
    });
}

} // namespace

void bind_aax_module(nb::module_& module) {
    nb::class_<cricodecs::aax::AaxSegmentInfo>(module, "AaxSegmentInfo")
        .def_ro("row_index", &cricodecs::aax::AaxSegmentInfo::row_index)
        .def_ro("data_size", &cricodecs::aax::AaxSegmentInfo::data_size)
        .def_ro("sample_count", &cricodecs::aax::AaxSegmentInfo::sample_count)
        .def_ro("loop_segment", &cricodecs::aax::AaxSegmentInfo::loop_segment);

    nb::class_<cricodecs::aax::AaxContainer>(module, "Aax")
        .def_static("load", [](const std::string& path) {
            return unwrap_expected(cricodecs::aax::AaxContainer::load(std::filesystem::path(path)));
        }, nb::arg("path"))
        .def_static("load_bytes", [](const nb::bytes& data) {
            const auto data_view = borrow_python_bytes(data);
            return unwrap_expected(cricodecs::aax::AaxContainer::load(as_byte_span(data_view)));
        }, nb::arg("data"))
        .def_prop_ro("source_path", [](const cricodecs::aax::AaxContainer& self) {
            return self.source_path().empty() ? std::string() : self.source_path().string();
        })
        .def_prop_ro("name", [](const cricodecs::aax::AaxContainer& self) {
            return std::string(self.name());
        })
        .def_prop_ro("segment_count", [](const cricodecs::aax::AaxContainer& self) {
            return self.segment_count();
        })
        .def_prop_ro("segments", [](const cricodecs::aax::AaxContainer& self) {
            return segment_list(self);
        })
        .def_prop_ro("channels", [](const cricodecs::aax::AaxContainer& self) {
            return self.channels();
        })
        .def_prop_ro("sample_rate", [](const cricodecs::aax::AaxContainer& self) {
            return self.sample_rate();
        })
        .def_prop_ro("sample_count", [](const cricodecs::aax::AaxContainer& self) {
            return self.sample_count();
        })
        .def_prop_ro("has_loop_segments", [](const cricodecs::aax::AaxContainer& self) {
            return self.has_loop_segments();
        })
        .def("info", [](const cricodecs::aax::AaxContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("name") = std::string(self.name());
            info.attr("segment_count") = self.segment_count();
            info.attr("channels") = self.channels();
            info.attr("sample_rate") = self.sample_rate();
            info.attr("sample_count") = self.sample_count();
            info.attr("has_loop_segments") = self.has_loop_segments();
            info.attr("segments") = segment_list(self);
            return info;
        })
        .def("segment", [](const cricodecs::aax::AaxContainer& self, uint32_t index) {
            const auto segments = self.segments();
            if (index >= segments.size()) {
                raise_value_error("AAX segment index is out of range");
            }
            return segments[index];
        }, nb::arg("index"))
        .def("segment_data", [](const cricodecs::aax::AaxContainer& self, uint32_t index) {
            auto bytes = unwrap_expected(self.segment_data(index));
            return to_python_bytes(bytes);
        }, nb::arg("index"))
        .def("segment_bytes", [](const cricodecs::aax::AaxContainer& self, uint32_t index) {
            auto bytes = unwrap_expected(self.segment_data(index));
            return to_python_bytes(bytes);
        }, nb::arg("index"))
        .def("extract_file", [](const cricodecs::aax::AaxContainer& self, uint32_t index, const nb::object& output_path) {
            unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path")));
        }, nb::arg("index"), nb::arg("output_path"))
        .def("adx_data", [](const cricodecs::aax::AaxContainer& self) {
            auto bytes = unwrap_expected(self.adx_data());
            return to_python_bytes(bytes);
        })
        .def("adx_bytes", [](const cricodecs::aax::AaxContainer& self) {
            auto bytes = unwrap_expected(self.adx_data());
            return to_python_bytes(bytes);
        })
        .def("extract", [](const cricodecs::aax::AaxContainer& self, const std::string& output_path) {
            unwrap_expected(self.extract(output_path));
        }, nb::arg("output_path"))
        .def("save_bytes", [](const cricodecs::aax::AaxContainer& self) {
            auto bytes = unwrap_expected(self.save());
            return to_python_bytes(bytes);
        })
        .def("save", [](const cricodecs::aax::AaxContainer& self) {
            auto bytes = unwrap_expected(self.save());
            return to_python_bytes(bytes);
        })
        .def("save", [](const cricodecs::aax::AaxContainer& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"));

    install_attr_repr(module, "AaxSegmentInfo", {"row_index", "data_size", "sample_count", "loop_segment"});
    install_attr_repr(module, "Aax", {"source_path", "name", "segment_count", "channels", "sample_rate", "sample_count", "has_loop_segments", "segments"});

    module.def("build",
        [](const nb::list& entries) {
            std::vector<cricodecs::aax::AaxBuildEntry> native_entries;
            native_entries.reserve(static_cast<size_t>(PyList_Size(entries.ptr())));
            for (auto item : entries) {
                auto data_object = nb::getattr(item, "adx_data");
                if (!PyBytes_Check(data_object.ptr())) {
                    raise_value_error("AAX build failed: each entry.adx_data must be bytes");
                }

                const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(data_object));
                cricodecs::aax::AaxBuildEntry entry;
                entry.adx_data.assign(
                    reinterpret_cast<const uint8_t*>(data_view.data()),
                    reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                );
                entry.loop_segment = nb::cast<bool>(nb::getattr(item, "loop_segment"));
                native_entries.push_back(std::move(entry));
            }

            auto built = unwrap_expected(cricodecs::aax::AaxContainer::build(native_entries));
            return to_python_bytes(built);
        },
        nb::arg("entries")
    );
    module.def("build_to_file",
        [](const nb::list& entries, const std::string& output_path) {
            std::vector<cricodecs::aax::AaxBuildEntry> native_entries;
            native_entries.reserve(static_cast<size_t>(PyList_Size(entries.ptr())));
            for (auto item : entries) {
                auto data_object = nb::getattr(item, "adx_data");
                if (!PyBytes_Check(data_object.ptr())) {
                    raise_value_error("AAX build failed: each entry.adx_data must be bytes");
                }

                const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(data_object));
                cricodecs::aax::AaxBuildEntry entry;
                entry.adx_data.assign(
                    reinterpret_cast<const uint8_t*>(data_view.data()),
                    reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                );
                entry.loop_segment = nb::cast<bool>(nb::getattr(item, "loop_segment"));
                native_entries.push_back(std::move(entry));
            }

            unwrap_expected(
                cricodecs::aax::AaxContainer::build_to_file(
                    native_entries,
                    std::filesystem::path(output_path)
                )
            );
        },
        nb::arg("entries"),
        nb::arg("output_path")
    );
    module.def("load", &load_aax_any, nb::arg("source"));
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto aax = load_aax_any(source);
        unwrap_expected(aax.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
