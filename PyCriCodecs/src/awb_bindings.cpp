#include "binding_helpers.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>

#include "../../CriCodecs/src/awb/awb_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] cricodecs::awb::AwbContainer load_awb_any(const nb::object& source) {
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::awb::AwbContainer::load(std::filesystem::path(*path)));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::awb::AwbContainer::load(std::move(owned)));
}

} // namespace

void bind_awb_module(nb::module_& module) {
    nb::enum_<cricodecs::awb::AacEncryptionState>(module, "AacEncryptionState")
        .value("CLEAR", cricodecs::awb::AacEncryptionState::Clear)
        .value("ENCRYPTED", cricodecs::awb::AacEncryptionState::Encrypted)
        .value("INDETERMINATE", cricodecs::awb::AacEncryptionState::Indeterminate);

    nb::class_<cricodecs::awb::AwbEntry>(module, "AwbEntry")
        .def_ro("wave_id", &cricodecs::awb::AwbEntry::wave_id)
        .def_ro("offset", &cricodecs::awb::AwbEntry::offset)
        .def_ro("size", &cricodecs::awb::AwbEntry::size);

    nb::class_<cricodecs::awb::AwbContainer>(module, "Awb")
        .def_static(
            "create",
            [](uint8_t version, uint16_t alignment, uint16_t subkey, uint8_t id_size, uint8_t offset_size) {
                return cricodecs::awb::AwbContainer::create(version, alignment, subkey, id_size, offset_size);
            },
            nb::arg("version") = cricodecs::awb::AwbContainer::DEFAULT_VERSION,
            nb::arg("alignment") = cricodecs::awb::AwbContainer::DEFAULT_ALIGNMENT,
            nb::arg("subkey") = static_cast<uint16_t>(0),
            nb::arg("id_size") = cricodecs::awb::AwbContainer::DEFAULT_ID_SIZE,
            nb::arg("offset_size") = cricodecs::awb::AwbContainer::DEFAULT_OFFSET_SIZE,
            "Create an empty editable AWB container."
        )
        .def_static(
            "load",
            [](const std::string& path) {
                return unwrap_expected(cricodecs::awb::AwbContainer::load(std::filesystem::path(path)));
            },
            nb::arg("path"),
            "Load an AWB from a filesystem path."
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data) {
                auto owned = copy_python_bytes(data);
                return unwrap_expected(cricodecs::awb::AwbContainer::load(std::move(owned)));
            },
            nb::arg("data"),
            "Load an AWB from raw bytes."
        )
        .def_prop_ro("source_path", [](const cricodecs::awb::AwbContainer& self) {
            return self.source_path().empty() ? std::string() : self.source_path().string();
        })
        .def_prop_ro("file_count", &cricodecs::awb::AwbContainer::file_count)
        .def_prop_ro("version", &cricodecs::awb::AwbContainer::version)
        .def_prop_ro("offset_size", &cricodecs::awb::AwbContainer::offset_size)
        .def_prop_ro("id_size", &cricodecs::awb::AwbContainer::id_size)
        .def_prop_ro("alignment", &cricodecs::awb::AwbContainer::alignment)
        .def_prop_ro("subkey", &cricodecs::awb::AwbContainer::subkey)
        .def_prop_ro("entries", [](const cricodecs::awb::AwbContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def("info", [](const cricodecs::awb::AwbContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("file_count") = self.file_count();
            info.attr("version") = self.version();
            info.attr("offset_size") = self.offset_size();
            info.attr("id_size") = self.id_size();
            info.attr("alignment") = self.alignment();
            info.attr("subkey") = self.subkey();
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            info.attr("entries") = entries;
            return info;
        })
        .def(
            "wave_id",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index) {
                return unwrap_expected(self.wave_id(index));
            },
            nb::arg("index")
        )
        .def(
            "entry_info",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index) {
                const auto& entries = self.entries();
                if (index >= entries.size()) {
                    raise_value_error("AWB entry index is out of range");
                }
                return entries[index];
            },
            nb::arg("index")
        )
        .def("entry_infos", [](const cricodecs::awb::AwbContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def(
            "file_bytes",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index) {
                auto bytes = unwrap_expected(self.file_bytes(index));
                return to_python_bytes(bytes);
            },
            nb::arg("index")
        )
        .def(
            "extract_file",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index, const nb::object& output_path) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path")));
            },
            nb::arg("index"),
            nb::arg("output_path")
        )
        .def(
            "extract",
            [](const cricodecs::awb::AwbContainer& self, const std::string& output_dir) {
                unwrap_expected(self.extract(std::filesystem::path(output_dir)));
            },
            nb::arg("output_dir")
        )
        .def(
            "save_bytes",
            [](cricodecs::awb::AwbContainer& self) {
                auto bytes = unwrap_expected(self.save());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](cricodecs::awb::AwbContainer& self) {
                auto bytes = unwrap_expected(self.save());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](cricodecs::awb::AwbContainer& self, const nb::object& output_path) {
                unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
            },
            nb::arg("output_path")
        )
        .def(
            "add_bytes",
            [](cricodecs::awb::AwbContainer& self, const nb::bytes& data, std::optional<uint64_t> wave_id) {
                const auto bytes = copy_python_bytes(data);
                if (wave_id.has_value()) {
                    self.add_file(std::span<const uint8_t>(bytes.data(), bytes.size()), *wave_id);
                    return *wave_id;
                }
                return self.add_file(std::span<const uint8_t>(bytes.data(), bytes.size()));
            },
            nb::arg("data"),
            nb::arg("wave_id") = nb::none()
        )
        .def(
            "add_file",
            [](cricodecs::awb::AwbContainer& self, const std::string& local_path, std::optional<uint64_t> wave_id) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    std::filesystem::path(local_path),
                    "AWB add_file failed"
                ));
                if (wave_id.has_value()) {
                    self.add_file(std::span<const uint8_t>(file_bytes.data(), file_bytes.size()), *wave_id);
                    return *wave_id;
                }
                return self.add_file(std::span<const uint8_t>(file_bytes.data(), file_bytes.size()));
            },
            nb::arg("local_path"),
            nb::arg("wave_id") = nb::none()
        )
        .def(
            "replace_bytes",
            [](cricodecs::awb::AwbContainer& self, uint32_t index, const nb::bytes& data) {
                const auto bytes = copy_python_bytes(data);
                unwrap_expected(self.replace_file(index, std::span<const uint8_t>(bytes.data(), bytes.size())));
            },
            nb::arg("index"),
            nb::arg("data")
        )
        .def(
            "replace_file",
            [](cricodecs::awb::AwbContainer& self, uint32_t index, const std::string& local_path) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    std::filesystem::path(local_path),
                    "AWB replace_file failed"
                ));
                unwrap_expected(self.replace_file(index, std::span<const uint8_t>(file_bytes.data(), file_bytes.size())));
            },
            nb::arg("index"),
            nb::arg("local_path")
        )
        .def(
            "remove",
            [](cricodecs::awb::AwbContainer& self, uint32_t index) {
                unwrap_expected(self.remove_file(index));
            },
            nb::arg("index")
        )
        .def(
            "set_wave_id",
            [](cricodecs::awb::AwbContainer& self, uint32_t index, uint64_t wave_id) {
                unwrap_expected(self.set_wave_id(index, wave_id));
            },
            nb::arg("index"),
            nb::arg("wave_id")
        )
        .def(
            "probe_aac_encryption",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index, uint64_t keycode) {
                return unwrap_expected(self.probe_aac_encryption(index, keycode));
            },
            nb::arg("index"),
            nb::arg("keycode")
        );

    install_attr_repr(module, "AwbEntry", {"wave_id", "offset", "size"});
    install_attr_repr(module, "Awb", {"source_path", "file_count", "version", "offset_size", "id_size", "alignment", "subkey", "entries"});

    module.def("load", &load_awb_any, nb::arg("source"));
    module.def(
        "create",
        [](uint8_t version, uint16_t alignment, uint16_t subkey, uint8_t id_size, uint8_t offset_size) {
            return cricodecs::awb::AwbContainer::create(version, alignment, subkey, id_size, offset_size);
        },
        nb::arg("version") = cricodecs::awb::AwbContainer::DEFAULT_VERSION,
        nb::arg("alignment") = cricodecs::awb::AwbContainer::DEFAULT_ALIGNMENT,
        nb::arg("subkey") = static_cast<uint16_t>(0),
        nb::arg("id_size") = cricodecs::awb::AwbContainer::DEFAULT_ID_SIZE,
        nb::arg("offset_size") = cricodecs::awb::AwbContainer::DEFAULT_OFFSET_SIZE
    );
    module.def("extract", [](const nb::object& source, const std::string& output_dir) {
        auto awb = load_awb_any(source);
        unwrap_expected(awb.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
