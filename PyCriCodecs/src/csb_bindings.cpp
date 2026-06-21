#include "binding_helpers.hpp"

#include <filesystem>
#include <vector>

#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/csb/csb_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::csb::CsbSection& section_at(
    const cricodecs::csb::CsbContainer& self,
    uint32_t index
) {
    if (index >= self.section_count()) {
        raise_value_error("CSB section index is out of range");
    }
    return self.section(index);
}

[[nodiscard]] const cricodecs::csb::CsbStreamInfo& element_at(
    const cricodecs::csb::CsbContainer& self,
    uint32_t index
) {
    if (index >= self.element_count()) {
        raise_value_error("CSB element index is out of range");
    }
    return self.element(index);
}

[[nodiscard]] const cricodecs::csb::CsbStreamInfo& stream_at(
    const cricodecs::csb::CsbContainer& self,
    uint32_t index
) {
    if (index >= self.stream_count()) {
        raise_value_error("CSB stream index is out of range");
    }
    return self.stream(index);
}

[[nodiscard]] cricodecs::csb::CsbContainer load_csb_any(const nb::object& source, nb::object encoding) {
    const auto options = python_encoding_options_from_object(encoding);
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::csb::CsbContainer::load(std::filesystem::path(*path), options));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::csb::CsbContainer::load(std::move(owned), options));
}

} // namespace

void bind_csb_module(nb::module_& module) {
    nb::class_<cricodecs::csb::CsbBuildEntry>(module, "CsbBuildEntry")
        .def(nb::init<>())
        .def("__init__", [](cricodecs::csb::CsbBuildEntry* self, const nb::object& source_path, const nb::object& archive_path) {
            cricodecs::csb::CsbBuildEntry entry;
            entry.source_path = require_python_path(source_path, "source_path");
            entry.archive_path = require_python_path(archive_path, "archive_path");
            new (self) cricodecs::csb::CsbBuildEntry(std::move(entry));
        }, nb::arg("source_path"), nb::arg("archive_path"))
        .def_rw("source_path", &cricodecs::csb::CsbBuildEntry::source_path)
        .def_rw("archive_path", &cricodecs::csb::CsbBuildEntry::archive_path);

    nb::class_<cricodecs::csb::CsbSection>(module, "CsbSection")
        .def_ro("row_index", &cricodecs::csb::CsbSection::row_index)
        .def_ro("name", &cricodecs::csb::CsbSection::name)
        .def_ro("table_type", &cricodecs::csb::CsbSection::table_type)
        .def_ro("data_size", &cricodecs::csb::CsbSection::data_size);

    nb::class_<cricodecs::csb::CsbStreamInfo>(module, "CsbStreamInfo")
        .def_ro("row_index", &cricodecs::csb::CsbStreamInfo::row_index)
        .def_ro("name", &cricodecs::csb::CsbStreamInfo::name)
        .def_prop_ro("name_raw", [](const cricodecs::csb::CsbStreamInfo& stream) {
            return string_to_python_bytes(stream.name_raw);
        })
        .def_ro("format", &cricodecs::csb::CsbStreamInfo::format)
        .def_ro("wrapper_table_name", &cricodecs::csb::CsbStreamInfo::wrapper_table_name)
        .def_ro("channels", &cricodecs::csb::CsbStreamInfo::channels)
        .def_ro("sample_rate", &cricodecs::csb::CsbStreamInfo::sample_rate)
        .def_ro("sample_count", &cricodecs::csb::CsbStreamInfo::sample_count)
        .def_ro("streamed", &cricodecs::csb::CsbStreamInfo::streamed)
        .def_ro("wrapper_size", &cricodecs::csb::CsbStreamInfo::wrapper_size)
        .def("suggested_path", [](const cricodecs::csb::CsbStreamInfo& stream) {
            return stream.suggested_path().generic_string();
        });

    nb::class_<cricodecs::csb::CsbContainer>(module, "Csb")
        .def_static("load", [](const std::string& path, nb::object encoding) {
            return unwrap_expected(cricodecs::csb::CsbContainer::load(
                std::filesystem::path(path),
                python_encoding_options_from_object(encoding)
            ));
        }, nb::arg("path"), nb::arg("encoding") = nb::none())
        .def_static("load_bytes", [](const nb::bytes& data, nb::object encoding) {
            auto owned = copy_python_bytes(data);
            return unwrap_expected(cricodecs::csb::CsbContainer::load(
                std::move(owned),
                python_encoding_options_from_object(encoding)
            ));
        }, nb::arg("data"), nb::arg("encoding") = nb::none())
        .def_prop_ro("name", [](const cricodecs::csb::CsbContainer& self) {
            return std::string(self.name());
        })
        .def_prop_ro("source_path", [](const cricodecs::csb::CsbContainer& self) {
            return self.source_path().empty() ? std::string() : self.source_path().string();
        })
        .def_prop_ro("section_count", &cricodecs::csb::CsbContainer::section_count)
        .def_prop_ro("element_count", &cricodecs::csb::CsbContainer::element_count)
        .def_prop_ro("stream_count", &cricodecs::csb::CsbContainer::stream_count)
        .def_prop_ro("sections", [](const cricodecs::csb::CsbContainer& self) {
            nb::list sections;
            for (const auto& section : self.sections()) {
                sections.append(section);
            }
            return sections;
        })
        .def_prop_ro("elements", [](const cricodecs::csb::CsbContainer& self) {
            nb::list elements;
            for (const auto& element : self.elements()) {
                elements.append(element);
            }
            return elements;
        })
        .def_prop_ro("streams", [](const cricodecs::csb::CsbContainer& self) {
            nb::list streams;
            for (uint32_t index = 0; index < self.stream_count(); ++index) {
                streams.append(self.stream(index));
            }
            return streams;
        })
        .def("info", [](const cricodecs::csb::CsbContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("name") = std::string(self.name());
            info.attr("section_count") = self.section_count();
            info.attr("element_count") = self.element_count();
            info.attr("stream_count") = self.stream_count();
            nb::list sections;
            for (const auto& section : self.sections()) {
                sections.append(section);
            }
            info.attr("sections") = sections;
            nb::list streams;
            for (uint32_t index = 0; index < self.stream_count(); ++index) {
                streams.append(self.stream(index));
            }
            info.attr("streams") = streams;
            return info;
        })
        .def("section", &section_at, nb::rv_policy::reference_internal, nb::arg("index"))
        .def("element", &element_at, nb::rv_policy::reference_internal, nb::arg("index"))
        .def("stream", &stream_at, nb::rv_policy::reference_internal, nb::arg("index"))
        .def("wrapper_bytes", [](const cricodecs::csb::CsbContainer& self, uint32_t index) {
            auto bytes = unwrap_expected(self.wrapper_data(index));
            return to_python_bytes(bytes);
        }, nb::arg("index"))
        .def("stream_bytes", [](const cricodecs::csb::CsbContainer& self, uint32_t index) {
            return to_python_bytes(unwrap_expected(self.stream_data(index)));
        }, nb::arg("index"))
        .def("extract_file", [](const cricodecs::csb::CsbContainer& self, uint32_t index, const std::string& output_path) {
            unwrap_expected(self.extract_file(index, std::filesystem::path(output_path)));
        }, nb::arg("index"), nb::arg("output_path"))
        .def("extract", [](const cricodecs::csb::CsbContainer& self, const std::string& output_dir) {
            unwrap_expected(self.extract(std::filesystem::path(output_dir)));
        }, nb::arg("output_dir"))
        .def("save", [](const cricodecs::csb::CsbContainer& self) {
            return to_python_bytes(unwrap_expected(self.save()));
        })
        .def("save", [](const cricodecs::csb::CsbContainer& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"))
        .def("save_to_file", [](const cricodecs::csb::CsbContainer& self, const nb::object& output_path) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"));

    install_attr_repr(module, "CsbBuildEntry", {"source_path", "archive_path"});
    install_attr_repr(module, "CsbSection", {"row_index", "name", "table_type", "data_size"});
    install_attr_repr(module, "CsbStreamInfo", {"row_index", "name", "format", "wrapper_table_name", "channels", "sample_rate", "sample_count", "streamed", "wrapper_size"});
    install_attr_repr(module, "Csb", {"source_path", "name", "section_count", "element_count", "stream_count", "sections", "streams"});

    module.def(
        "build_entries",
        [](const std::vector<cricodecs::csb::CsbBuildEntry>& entries, nb::object encoding) {
            return to_python_bytes(unwrap_expected(cricodecs::csb::CsbContainer::build(
                entries,
                python_encoding_options_from_object(encoding)
            )));
        },
        nb::arg("entries"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "build",
        [](const std::vector<cricodecs::csb::CsbBuildEntry>& entries, nb::object encoding) {
            return to_python_bytes(unwrap_expected(cricodecs::csb::CsbContainer::build(
                entries,
                python_encoding_options_from_object(encoding)
            )));
        },
        nb::arg("entries"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "build_entries_to_file",
        [](const std::vector<cricodecs::csb::CsbBuildEntry>& entries, const std::string& output_path, nb::object encoding) {
            unwrap_expected(cricodecs::csb::CsbContainer::build_to_file(
                entries,
                std::filesystem::path(output_path),
                python_encoding_options_from_object(encoding)
            ));
        },
        nb::arg("entries"),
        nb::arg("output_path"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "build",
        [](const std::vector<cricodecs::csb::CsbBuildEntry>& entries, const nb::object& output_path, nb::object encoding) {
            unwrap_expected(cricodecs::csb::CsbContainer::build_to_file(
                entries,
                require_python_path(output_path, "output_path"),
                python_encoding_options_from_object(encoding)
            ));
        },
        nb::arg("entries"),
        nb::arg("output_path"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "build_from_directory",
        [](const std::string& input_dir, nb::object encoding) {
            return to_python_bytes(unwrap_expected(cricodecs::csb::CsbContainer::build_from_directory(
                std::filesystem::path(input_dir),
                python_encoding_options_from_object(encoding)
            )));
        },
        nb::arg("input_dir"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "build_directory_to_file",
        [](const std::string& input_dir, const std::string& output_path, nb::object encoding) {
            unwrap_expected(cricodecs::csb::CsbContainer::build_to_file(
                std::filesystem::path(input_dir),
                std::filesystem::path(output_path),
                python_encoding_options_from_object(encoding)
            ));
        },
        nb::arg("input_dir"),
        nb::arg("output_path"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "load",
        &load_csb_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "extract",
        [](const nb::object& source, const std::string& output_dir, nb::object encoding) {
            auto csb = load_csb_any(source, encoding);
            unwrap_expected(csb.extract(std::filesystem::path(output_dir)));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python
