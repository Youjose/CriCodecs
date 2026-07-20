#include "binding_helpers.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include <nanobind/stl/filesystem.h>

#include "../../CriCodecs/src/acx/acx_builder.hpp"
#include "../../CriCodecs/src/acx/acx_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] cricodecs::acx::AcxContainer load_acx_path(const std::string& path) {
    return unwrap_expected(cricodecs::acx::AcxContainer::load(std::filesystem::path(path)));
}

[[nodiscard]] cricodecs::acx::AcxContainer load_acx_bytes(const nb::bytes& data) {
    auto borrowed = borrow_python_source(data);
    auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::acx::AcxContainer::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::acx::AcxContainer load_acx_source(const nb::object& source) {
    auto borrowed = borrow_python_source(source);
    auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::acx::AcxContainer::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::acx::AcxContainer load_acx_any(const nb::object& source) {
    if (auto path = python_text_path(source)) {
        return load_acx_path(*path);
    }
    return load_acx_source(source);
}

} // namespace

void bind_acx_module(nb::module_& module) {
    nb::enum_<cricodecs::acx::AcxEntryType>(module, "AcxEntryType")
        .value("UNKNOWN", cricodecs::acx::AcxEntryType::unknown)
        .value("ADX", cricodecs::acx::AcxEntryType::adx)
        .value("OGG", cricodecs::acx::AcxEntryType::ogg);

    nb::class_<cricodecs::acx::AcxEntry>(module, "AcxEntry")
        .def_ro("index", &cricodecs::acx::AcxEntry::index)
        .def_ro("offset", &cricodecs::acx::AcxEntry::offset)
        .def_ro("size", &cricodecs::acx::AcxEntry::size)
        .def_ro("type", &cricodecs::acx::AcxEntry::type)
        .def(
            "suggested_path",
            [](const cricodecs::acx::AcxEntry& entry, bool include_index_prefix) {
                return entry.suggested_path(include_index_prefix).generic_string();
            },
            nb::arg("include_index_prefix") = true
        );

    nb::class_<cricodecs::acx::AcxBuildEntry>(module, "AcxBuildEntry")
        .def(nb::init<>())
        .def("__init__", [](cricodecs::acx::AcxBuildEntry* self, const nb::object& source) {
            cricodecs::acx::AcxBuildEntry entry;
            if (auto path = python_text_path(source)) {
                entry.source_path = std::filesystem::path(*path);
            } else {
                auto borrowed = borrow_python_source(source);
                const auto data = borrowed.as_span();
                entry.data = std::vector<uint8_t>(data.begin(), data.end());
            }
            new (self) cricodecs::acx::AcxBuildEntry(std::move(entry));
        }, nb::arg("source"))
        .def_prop_rw("source_path", [](const cricodecs::acx::AcxBuildEntry& self) {
            return self.source_path.empty() ? nb::none() : nb::cast(self.source_path.generic_string());
        }, [](cricodecs::acx::AcxBuildEntry& self, const nb::object& value) {
            self.source_path = value.is_none() ? std::filesystem::path{} : require_python_path(value, "source_path");
        })
        .def_prop_rw("data", [](const cricodecs::acx::AcxBuildEntry& self) -> nb::object {
            if (!self.data.has_value()) {
                return nb::none();
            }
            return to_python_bytes(*self.data);
        }, [](cricodecs::acx::AcxBuildEntry& self, const nb::object& value) {
            if (value.is_none()) {
                self.data.reset();
                return;
            }
            auto borrowed = borrow_python_source(value);
            const auto data = borrowed.as_span();
            self.data = std::vector<uint8_t>(data.begin(), data.end());
        });

    nb::class_<cricodecs::acx::AcxContainer>(module, "Acx")
        .def_static(
            "load",
            &load_acx_any,
            nb::arg("source"),
            "Load an ACX archive from a path, buffer-backed object, or binary file-like object."
        )
        .def_static(
            "load_bytes",
            &load_acx_bytes,
            nb::arg("data"),
            "Load an ACX archive from raw bytes."
        )
        .def_static(
            "load_source",
            &load_acx_source,
            nb::arg("source"),
            "Load an ACX archive from a buffer-backed Python object."
        )
        .def_prop_ro("source_path", [](const cricodecs::acx::AcxContainer& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("entry_count", [](const cricodecs::acx::AcxContainer& self) {
            return self.entry_count();
        })
        .def_prop_ro("table_size", [](const cricodecs::acx::AcxContainer& self) {
            return self.table_size();
        })
        .def_prop_ro("first_payload_offset", [](const cricodecs::acx::AcxContainer& self) -> nb::object {
            const auto offset = self.first_payload_offset();
            if (!offset.has_value()) {
                return nb::none();
            }
            return nb::cast(*offset);
        })
        .def_prop_ro("payload_end_offset", [](const cricodecs::acx::AcxContainer& self) -> nb::object {
            const auto offset = self.payload_end_offset();
            if (!offset.has_value()) {
                return nb::none();
            }
            return nb::cast(*offset);
        })
        .def_prop_ro("entries", [](const cricodecs::acx::AcxContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def("info", [](const cricodecs::acx::AcxContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("entry_count") = self.entry_count();
            info.attr("table_size") = self.table_size();
            info.attr("first_payload_offset") = self.first_payload_offset().has_value() ? nb::cast(*self.first_payload_offset()) : nb::none();
            info.attr("payload_end_offset") = self.payload_end_offset().has_value() ? nb::cast(*self.payload_end_offset()) : nb::none();
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            info.attr("entries") = entries;
            return info;
        })
        .def("entry", [](const cricodecs::acx::AcxContainer& self, uint32_t index) {
            const auto& entries = self.entries();
            if (index >= entries.size()) {
                raise_value_error("ACX entry index is out of range");
            }
            return entries[index];
        }, nb::arg("index"))
        .def("type_count", &cricodecs::acx::AcxContainer::type_count, nb::arg("type"))
        .def(
            "file_bytes",
            [](const cricodecs::acx::AcxContainer& self, uint32_t index) {
                auto bytes = unwrap_expected(self.file_data(index));
                return to_python_bytes(bytes);
            },
            nb::arg("index")
        )
        .def(
            "extract_file",
            [](const cricodecs::acx::AcxContainer& self, uint32_t index, const nb::object& output_path) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path")));
            },
            nb::arg("index"),
            nb::arg("output_path")
        )
        .def(
            "extract",
            [](const cricodecs::acx::AcxContainer& self, const nb::object& output_dir) {
                unwrap_expected(self.extract(require_python_path(output_dir, "output_dir")));
            },
            nb::arg("output_dir")
        )
        .def(
            "rebuild",
            [](const cricodecs::acx::AcxContainer& self) {
                auto bytes = unwrap_expected(self.rebuild());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](const cricodecs::acx::AcxContainer& self) {
                auto bytes = unwrap_expected(self.rebuild());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](const cricodecs::acx::AcxContainer& self, const nb::object& output_path) {
                unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
            },
            nb::arg("output_path")
        )
        .def(
            "set_file_data",
            [](cricodecs::acx::AcxContainer& self, uint32_t index, const nb::bytes& data) {
                unwrap_expected(self.set_file_data(index, as_byte_span(borrow_python_bytes(data))));
            },
            nb::arg("index"),
            nb::arg("data")
        )
        .def(
            "add_file",
            [](cricodecs::acx::AcxContainer& self, const nb::bytes& data) {
                unwrap_expected(self.add_file(as_byte_span(borrow_python_bytes(data))));
            },
            nb::arg("data")
        )
        .def(
            "remove_file",
            [](cricodecs::acx::AcxContainer& self, uint32_t index) {
                unwrap_expected(self.remove_file(index));
            },
            nb::arg("index")
        )
        .def(
            "move_file",
            [](cricodecs::acx::AcxContainer& self, uint32_t from_index, uint32_t to_index) {
                unwrap_expected(self.move_file(from_index, to_index));
            },
            nb::arg("from_index"),
            nb::arg("to_index")
        );

    install_attr_repr(module, "AcxEntry", {"index", "offset", "size", "type"});
    install_attr_repr(module, "AcxBuildEntry", {"source_path", "data"});
    install_attr_repr(module, "Acx", {"source_path", "entry_count", "table_size", "first_payload_offset", "payload_end_offset", "entries"});

    module.def(
        "build",
        [](const std::filesystem::path& file_list_path, uint32_t alignment) {
            cricodecs::acx::AcxBuilder builder;
            return to_python_bytes(unwrap_expected(
                builder.build_from_file_list(file_list_path, alignment)
            ));
        },
        nb::arg("file_list_path"),
        nb::arg("alignment") = 4
    );
    module.def(
        "build",
        [](const std::filesystem::path& file_list_path, const std::filesystem::path& output_path, uint32_t alignment) {
            cricodecs::acx::AcxBuilder builder;
            unwrap_expected(builder.build_file_list_to_file(
                file_list_path,
                output_path,
                alignment
            ));
        },
        nb::arg("file_list_path"),
        nb::arg("output_path"),
        nb::arg("alignment") = 4
    );

    module.def(
        "build",
        [](const nb::list& entries, uint32_t alignment) {
            cricodecs::acx::AcxBuildInput input;
            input.alignment = alignment;
            input.entries.reserve(static_cast<size_t>(PyList_Size(entries.ptr())));

            for (size_t index = 0; index < static_cast<size_t>(PyList_Size(entries.ptr())); ++index) {
                auto item = entries[index];
                cricodecs::acx::AcxBuildEntry entry;

                if (nb::hasattr(item, "source_path")) {
                    auto source_path = nb::getattr(item, "source_path");
                    if (!source_path.is_none()) {
                        entry.source_path = nb::cast<std::string>(source_path);
                    }
                }
                if (nb::hasattr(item, "data")) {
                    auto data_object = nb::getattr(item, "data");
                    if (!data_object.is_none()) {
                        if (!PyBytes_Check(data_object.ptr())) {
                            raise_value_error("ACX build failed: each entry.data must already be bytes");
                        }
                        const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(data_object));
                        entry.data = std::vector<uint8_t>(
                            reinterpret_cast<const uint8_t*>(data_view.data()),
                            reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                        );
                    }
                }

                if (entry.source_path.empty() && !entry.data.has_value()) {
                    raise_value_error("ACX build failed: each entry needs source_path or data");
                }
                input.entries.push_back(std::move(entry));
            }

            cricodecs::acx::AcxBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("entries"),
        nb::arg("alignment") = 4
    );
    module.def(
        "build",
        [](const nb::list& entries, const nb::object& output_path, uint32_t alignment) {
            cricodecs::acx::AcxBuildInput input;
            input.alignment = alignment;
            input.entries.reserve(static_cast<size_t>(PyList_Size(entries.ptr())));
            for (size_t index = 0; index < static_cast<size_t>(PyList_Size(entries.ptr())); ++index) {
                auto item = entries[index];
                input.entries.push_back(nb::cast<cricodecs::acx::AcxBuildEntry>(item));
            }
            cricodecs::acx::AcxBuilder builder;
            unwrap_expected(builder.build_to_file(require_python_path(output_path, "output_path"), input));
        },
        nb::arg("entries"),
        nb::arg("output_path"),
        nb::arg("alignment") = 4
    );
    module.def(
        "build_to_file",
        [](const nb::list& entries, const std::string& output_path, uint32_t alignment) {
            cricodecs::acx::AcxBuildInput input;
            input.alignment = alignment;
            input.entries.reserve(static_cast<size_t>(PyList_Size(entries.ptr())));

            for (size_t index = 0; index < static_cast<size_t>(PyList_Size(entries.ptr())); ++index) {
                auto item = entries[index];
                cricodecs::acx::AcxBuildEntry entry;

                if (nb::hasattr(item, "source_path")) {
                    auto source_path = nb::getattr(item, "source_path");
                    if (!source_path.is_none()) {
                        entry.source_path = nb::cast<std::string>(source_path);
                    }
                }
                if (nb::hasattr(item, "data")) {
                    auto data_object = nb::getattr(item, "data");
                    if (!data_object.is_none()) {
                        if (!PyBytes_Check(data_object.ptr())) {
                            raise_value_error("ACX build failed: each entry.data must already be bytes");
                        }
                        const auto data_view = borrow_python_bytes(nb::borrow<nb::bytes>(data_object));
                        entry.data = std::vector<uint8_t>(
                            reinterpret_cast<const uint8_t*>(data_view.data()),
                            reinterpret_cast<const uint8_t*>(data_view.data()) + data_view.size()
                        );
                    }
                }

                if (entry.source_path.empty() && !entry.data.has_value()) {
                    raise_value_error("ACX build failed: each entry needs source_path or data");
                }
                input.entries.push_back(std::move(entry));
            }

            cricodecs::acx::AcxBuilder builder;
            unwrap_expected(builder.build_to_file(std::filesystem::path(output_path), input));
        },
        nb::arg("entries"),
        nb::arg("output_path"),
        nb::arg("alignment") = 4
    );
    module.def(
        "build_from_file_list",
        [](const std::string& file_list_path, uint32_t alignment) {
            cricodecs::acx::AcxBuilder builder;
            return to_python_bytes(unwrap_expected(
                builder.build_from_file_list(std::filesystem::path(file_list_path), alignment)
            ));
        },
        nb::arg("file_list_path"),
        nb::arg("alignment") = 4
    );
    module.def(
        "build_file_list_to_file",
        [](const std::string& file_list_path, const std::string& output_path, uint32_t alignment) {
            cricodecs::acx::AcxBuilder builder;
            unwrap_expected(builder.build_file_list_to_file(
                std::filesystem::path(file_list_path),
                std::filesystem::path(output_path),
                alignment
            ));
        },
        nb::arg("file_list_path"),
        nb::arg("output_path"),
        nb::arg("alignment") = 4
    );
    module.def("load", &load_acx_any, nb::arg("source"));
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto acx = load_acx_any(source);
        unwrap_expected(acx.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
