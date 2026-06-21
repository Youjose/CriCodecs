#include "binding_helpers.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>

#include "../../CriCodecs/src/afs/afs_container.hpp"
#include "../../CriCodecs/src/utilities/io.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] std::array<uint8_t, 12> require_directory_metadata(const nb::bytes& data) {
    const auto bytes = copy_python_bytes(data);
    if (bytes.size() != 12) {
        raise_value_error("AFS directory metadata must be exactly 12 bytes");
    }

    std::array<uint8_t, 12> metadata{};
    std::ranges::copy(bytes, metadata.begin());
    return metadata;
}

[[nodiscard]] cricodecs::afs::AfsContainer load_afs_path(const std::string& path) {
    return unwrap_expected(cricodecs::afs::AfsContainer::load(std::filesystem::path(path)));
}

[[nodiscard]] cricodecs::afs::AfsContainer load_afs_bytes(const nb::bytes& data) {
    auto borrowed = borrow_python_source(data);
    const auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::afs::AfsContainer::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::afs::AfsContainer load_afs_source(const nb::object& source) {
    auto borrowed = borrow_python_source(source);
    const auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::afs::AfsContainer::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::afs::AfsContainer load_afs_any(const nb::object& source) {
    if (auto path = python_text_path(source)) {
        return load_afs_path(*path);
    }
    return load_afs_source(source);
}

[[nodiscard]] cricodecs::afs::AfsContainer create_afs(
    uint32_t alignment,
    bool include_directory_table
) {
    return cricodecs::afs::AfsContainer::create(alignment, include_directory_table);
}

[[nodiscard]] cricodecs::afs::AfsContainer create_afs_from_als(
    const std::string& als_path,
    uint32_t alignment,
    bool include_directory_table,
    std::optional<std::string> source_root
) {
    return unwrap_expected(cricodecs::afs::AfsContainer::create_from_als(
        std::filesystem::path(als_path),
        alignment,
        include_directory_table,
        source_root.has_value() ? std::optional(std::filesystem::path(*source_root)) : std::nullopt
    ));
}

} // namespace

void bind_afs_module(nb::module_& module) {
    nb::enum_<cricodecs::afs::AfsEntryType>(module, "AfsEntryType")
        .value("UNKNOWN", cricodecs::afs::AfsEntryType::unknown)
        .value("ADX", cricodecs::afs::AfsEntryType::adx)
        .value("OGG", cricodecs::afs::AfsEntryType::ogg)
        .value("HCA", cricodecs::afs::AfsEntryType::hca);

    nb::enum_<cricodecs::afs::AfsHeaderNameMode>(module, "AfsHeaderNameMode")
        .value("FILENAME_ONLY", cricodecs::afs::AfsHeaderNameMode::filename_only)
        .value("CUT_OVERLAPPING_STRING", cricodecs::afs::AfsHeaderNameMode::cut_overlapping_string)
        .value("FULL_PATH", cricodecs::afs::AfsHeaderNameMode::full_path);

    nb::class_<cricodecs::afs::AfsDirectoryTimestamp>(module, "AfsDirectoryTimestamp")
        .def(nb::init<>())
        .def_rw("year", &cricodecs::afs::AfsDirectoryTimestamp::year)
        .def_rw("month", &cricodecs::afs::AfsDirectoryTimestamp::month)
        .def_rw("day", &cricodecs::afs::AfsDirectoryTimestamp::day)
        .def_rw("hour", &cricodecs::afs::AfsDirectoryTimestamp::hour)
        .def_rw("minute", &cricodecs::afs::AfsDirectoryTimestamp::minute)
        .def_rw("second", &cricodecs::afs::AfsDirectoryTimestamp::second)
        .def("empty", &cricodecs::afs::AfsDirectoryTimestamp::empty);

    nb::class_<cricodecs::afs::AfsEntry>(module, "AfsEntry")
        .def_ro("index", &cricodecs::afs::AfsEntry::index)
        .def_ro("offset", &cricodecs::afs::AfsEntry::offset)
        .def_ro("size", &cricodecs::afs::AfsEntry::size)
        .def_ro("present", &cricodecs::afs::AfsEntry::present)
        .def_ro("type", &cricodecs::afs::AfsEntry::type)
        .def_prop_ro("name", [](const cricodecs::afs::AfsEntry& entry) -> nb::object {
            if (!entry.name.has_value()) {
                return nb::none();
            }
            return nb::cast(*entry.name);
        })
        .def_prop_ro("header_source_name", [](const cricodecs::afs::AfsEntry& entry) -> nb::object {
            if (!entry.header_source_name.has_value()) {
                return nb::none();
            }
            return nb::cast(*entry.header_source_name);
        })
        .def_prop_ro("directory_metadata", [](const cricodecs::afs::AfsEntry& entry) {
            return to_python_bytes(std::span<const uint8_t>(entry.directory_metadata.data(), entry.directory_metadata.size()));
        })
        .def("is_present", &cricodecs::afs::AfsEntry::is_present)
        .def(
            "suggested_path",
            [](const cricodecs::afs::AfsEntry& entry, bool include_index_prefix) {
                return entry.suggested_path(include_index_prefix).generic_string();
            },
            nb::arg("include_index_prefix") = true
        )
        .def("directory_timestamp", [](const cricodecs::afs::AfsEntry& entry) -> nb::object {
            const auto timestamp = entry.directory_timestamp();
            if (!timestamp.has_value()) {
                return nb::none();
            }
            return nb::cast(*timestamp);
        });

    nb::class_<cricodecs::afs::AfsContainer>(module, "Afs")
        .def_static(
            "load",
            &load_afs_any,
            nb::arg("source"),
            "Load an AFS archive from a path, buffer-backed object, or binary file-like object."
        )
        .def_static(
            "load_bytes",
            &load_afs_bytes,
            nb::arg("data"),
            "Load an AFS archive from raw bytes."
        )
        .def_static(
            "load_source",
            &load_afs_source,
            nb::arg("source"),
            "Load an AFS archive from a buffer-backed Python object."
        )
        .def_static(
            "create",
            &create_afs,
            nb::arg("alignment") = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT,
            nb::arg("include_directory_table") = false,
            "Create an empty editable AFS container."
        )
        .def_static(
            "create_from_als",
            &create_afs_from_als,
            nb::arg("als_path"),
            nb::arg("alignment") = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT,
            nb::arg("include_directory_table") = false,
            nb::arg("source_root") = nb::none(),
            "Create an editable AFS container from an ALS build script."
        )
        .def_prop_ro("source_path", [](const cricodecs::afs::AfsContainer& self) {
            return self.source_path().empty() ? std::string() : self.source_path().string();
        })
        .def_prop_ro("entry_count", [](const cricodecs::afs::AfsContainer& self) {
            return self.entry_count();
        })
        .def_prop_ro("present_entry_count", [](const cricodecs::afs::AfsContainer& self) {
            return self.present_entry_count();
        })
        .def_prop_ro("alignment", [](const cricodecs::afs::AfsContainer& self) {
            return self.alignment();
        })
        .def_prop_ro("has_directory_table", [](const cricodecs::afs::AfsContainer& self) {
            return self.has_directory_table();
        })
        .def_prop_ro("directory_table_offset", [](const cricodecs::afs::AfsContainer& self) -> nb::object {
            const auto offset = self.directory_table_offset();
            if (!offset.has_value()) {
                return nb::none();
            }
            return nb::cast(*offset);
        })
        .def_prop_ro("directory_table_size", [](const cricodecs::afs::AfsContainer& self) -> nb::object {
            const auto size = self.directory_table_size();
            if (!size.has_value()) {
                return nb::none();
            }
            return nb::cast(*size);
        })
        .def_prop_ro("first_payload_offset", [](const cricodecs::afs::AfsContainer& self) -> nb::object {
            const auto offset = self.first_payload_offset();
            if (!offset.has_value()) {
                return nb::none();
            }
            return nb::cast(*offset);
        })
        .def_prop_ro("is_materialized", [](const cricodecs::afs::AfsContainer& self) {
            return self.is_materialized();
        })
        .def_prop_ro("entries", [](const cricodecs::afs::AfsContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def("info", [](const cricodecs::afs::AfsContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("entry_count") = self.entry_count();
            info.attr("present_entry_count") = self.present_entry_count();
            info.attr("alignment") = self.alignment();
            info.attr("has_directory_table") = self.has_directory_table();
            info.attr("directory_table_offset") = self.directory_table_offset().has_value() ? nb::cast(*self.directory_table_offset()) : nb::none();
            info.attr("directory_table_size") = self.directory_table_size().has_value() ? nb::cast(*self.directory_table_size()) : nb::none();
            info.attr("first_payload_offset") = self.first_payload_offset().has_value() ? nb::cast(*self.first_payload_offset()) : nb::none();
            info.attr("is_materialized") = self.is_materialized();
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            info.attr("entries") = entries;
            return info;
        })
        .def("entry", [](const cricodecs::afs::AfsContainer& self, uint32_t index) {
            const auto& entries = self.entries();
            if (index >= entries.size()) {
                raise_value_error("AFS entry index is out of range");
            }
            return entries[index];
        }, nb::arg("index"))
        .def(
            "file_bytes",
            [](const cricodecs::afs::AfsContainer& self, uint32_t index) {
                auto bytes = unwrap_expected(self.file_data(index));
                return to_python_bytes(bytes);
            },
            nb::arg("index")
        )
        .def(
            "extract_file",
            [](const cricodecs::afs::AfsContainer& self, uint32_t index, const nb::object& output_path) {
                unwrap_expected(self.export_stream(index, require_python_path(output_path, "output_path")));
            },
            nb::arg("index"),
            nb::arg("output_path")
        )
        .def(
            "extract",
            [](const cricodecs::afs::AfsContainer& self, const std::string& output_dir) {
                unwrap_expected(self.extract(std::filesystem::path(output_dir)));
            },
            nb::arg("output_dir")
        )
        .def(
            "save_bytes",
            [](cricodecs::afs::AfsContainer& self) {
                return to_python_bytes(unwrap_expected(self.build()));
            }
        )
        .def(
            "save",
            [](cricodecs::afs::AfsContainer& self) {
                return to_python_bytes(unwrap_expected(self.build()));
            }
        )
        .def(
            "save",
            [](cricodecs::afs::AfsContainer& self, const nb::object& output_path) {
                unwrap_expected(self.build_to_file(require_python_path(output_path, "output_path")));
            },
            nb::arg("output_path")
        )
        .def(
            "add_bytes",
            [](cricodecs::afs::AfsContainer& self, const nb::bytes& data, std::optional<std::string> name, const nb::bytes& directory_metadata) {
                const auto bytes = copy_python_bytes(data);
                self.add_file(
                    std::span<const uint8_t>(bytes.data(), bytes.size()),
                    std::move(name),
                    require_directory_metadata(directory_metadata)
                );
            },
            nb::arg("data"),
            nb::arg("name") = nb::none(),
            nb::arg("directory_metadata") = nb::bytes("\0\0\0\0\0\0\0\0\0\0\0\0", 12)
        )
        .def(
            "add_file",
            [](cricodecs::afs::AfsContainer& self, const std::string& local_path, std::optional<std::string> name, const nb::bytes& directory_metadata) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    std::filesystem::path(local_path),
                    "AFS add_file failed"
                ));
                self.add_file(
                    std::span<const uint8_t>(file_bytes.data(), file_bytes.size()),
                    std::move(name),
                    require_directory_metadata(directory_metadata)
                );
            },
            nb::arg("local_path"),
            nb::arg("name") = nb::none(),
            nb::arg("directory_metadata") = nb::bytes("\0\0\0\0\0\0\0\0\0\0\0\0", 12)
        )
        .def(
            "add_bytes_at_id",
            [](cricodecs::afs::AfsContainer& self, uint32_t file_id, const nb::bytes& data, std::optional<std::string> name, const nb::bytes& directory_metadata) {
                const auto bytes = copy_python_bytes(data);
                self.add_file_at_id(
                    file_id,
                    std::span<const uint8_t>(bytes.data(), bytes.size()),
                    std::move(name),
                    require_directory_metadata(directory_metadata)
                );
            },
            nb::arg("file_id"),
            nb::arg("data"),
            nb::arg("name") = nb::none(),
            nb::arg("directory_metadata") = nb::bytes("\0\0\0\0\0\0\0\0\0\0\0\0", 12)
        )
        .def(
            "add_file_at_id",
            [](cricodecs::afs::AfsContainer& self, uint32_t file_id, const std::string& local_path, std::optional<std::string> name, const nb::bytes& directory_metadata) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    std::filesystem::path(local_path),
                    "AFS add_file_at_id failed"
                ));
                self.add_file_at_id(
                    file_id,
                    std::span<const uint8_t>(file_bytes.data(), file_bytes.size()),
                    std::move(name),
                    require_directory_metadata(directory_metadata)
                );
            },
            nb::arg("file_id"),
            nb::arg("local_path"),
            nb::arg("name") = nb::none(),
            nb::arg("directory_metadata") = nb::bytes("\0\0\0\0\0\0\0\0\0\0\0\0", 12)
        )
        .def(
            "reserve_file_id",
            [](cricodecs::afs::AfsContainer& self, uint32_t file_id) {
                self.reserve_file_id(file_id);
            },
            nb::arg("file_id")
        )
        .def(
            "materialize",
            [](cricodecs::afs::AfsContainer& self) {
                unwrap_expected(self.materialize());
            }
        )
        .def(
            "replace_bytes",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, const nb::bytes& data) {
                const auto bytes = copy_python_bytes(data);
                unwrap_expected(self.replace_file(index, std::span<const uint8_t>(bytes.data(), bytes.size())));
            },
            nb::arg("index"),
            nb::arg("data")
        )
        .def(
            "replace_file",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, const std::string& local_path) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    std::filesystem::path(local_path),
                    "AFS replace_file failed"
                ));
                unwrap_expected(self.replace_file(index, std::span<const uint8_t>(file_bytes.data(), file_bytes.size())));
            },
            nb::arg("index"),
            nb::arg("local_path")
        )
        .def(
            "set_name",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, std::optional<std::string> name) {
                unwrap_expected(self.set_name(index, std::move(name)));
            },
            nb::arg("index"),
            nb::arg("name") = nb::none()
        )
        .def(
            "set_header_source_name",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, std::optional<std::string> header_source_name) {
                unwrap_expected(self.set_header_source_name(index, std::move(header_source_name)));
            },
            nb::arg("index"),
            nb::arg("header_source_name") = nb::none()
        )
        .def(
            "set_directory_metadata",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, const nb::bytes& directory_metadata) {
                unwrap_expected(self.set_directory_metadata(index, require_directory_metadata(directory_metadata)));
            },
            nb::arg("index"),
            nb::arg("directory_metadata")
        )
        .def(
            "set_directory_timestamp",
            [](cricodecs::afs::AfsContainer& self, uint32_t index, std::optional<cricodecs::afs::AfsDirectoryTimestamp> timestamp) {
                unwrap_expected(self.set_directory_timestamp(index, timestamp));
            },
            nb::arg("index"),
            nb::arg("timestamp") = nb::none()
        )
        .def(
            "set_alignment",
            [](cricodecs::afs::AfsContainer& self, uint32_t alignment) {
                unwrap_expected(self.set_alignment(alignment));
            },
            nb::arg("alignment")
        )
        .def(
            "set_first_payload_offset",
            [](cricodecs::afs::AfsContainer& self, std::optional<uint32_t> offset) {
                unwrap_expected(self.set_first_payload_offset(offset));
            },
            nb::arg("offset") = nb::none()
        )
        .def(
            "set_directory_table_enabled",
            [](cricodecs::afs::AfsContainer& self, bool enabled) {
                self.set_directory_table_enabled(enabled);
            },
            nb::arg("enabled")
        )
        .def(
            "build_file_id_header",
            [](const cricodecs::afs::AfsContainer& self,
               const std::string& archive_name,
               const std::string& id_prefix,
               cricodecs::afs::AfsHeaderNameMode name_mode) {
                return unwrap_expected(self.build_file_id_header(archive_name, id_prefix, name_mode));
            },
            nb::arg("archive_name"),
            nb::arg("id_prefix") = "",
            nb::arg("name_mode") = cricodecs::afs::AfsHeaderNameMode::filename_only
        );

    install_attr_repr(module, "AfsDirectoryTimestamp", {"year", "month", "day", "hour", "minute", "second"});
    install_attr_repr(module, "AfsEntry", {"index", "offset", "size", "present", "type", "name", "header_source_name"});
    install_attr_repr(module, "Afs", {"source_path", "entry_count", "present_entry_count", "alignment", "has_directory_table", "directory_table_offset", "directory_table_size", "first_payload_offset", "is_materialized", "entries"});

    module.def(
        "create",
        &create_afs,
        nb::arg("alignment") = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT,
        nb::arg("include_directory_table") = false
    );
    module.def(
        "create_from_als",
        &create_afs_from_als,
        nb::arg("als_path"),
        nb::arg("alignment") = cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT,
        nb::arg("include_directory_table") = false,
        nb::arg("source_root") = nb::none()
    );
    module.def("load", &load_afs_any, nb::arg("source"));
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto afs = load_afs_any(source);
        unwrap_expected(afs.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
