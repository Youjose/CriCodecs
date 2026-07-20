#include "binding_helpers.hpp"

#include <filesystem>

#include "../../CriCodecs/src/acb/acb_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::acb::AcbContainer& checked_container(
    const cricodecs::acb::AcbContainer& self,
    uint32_t index) {
    if (index >= self.waveform_count()) {
        raise_value_error("ACB waveform index is out of range");
    }
    return self;
}

[[nodiscard]] cricodecs::acb::AcbContainer load_acb_any(const nb::object& source, nb::object encoding) {
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::acb::AcbContainer::load(
            std::filesystem::path(*path),
            python_encoding_options_from_object(encoding)
        ));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::acb::AcbContainer::load(
        std::move(owned),
        python_encoding_options_from_object(encoding)
    ));
}

} // namespace

void bind_acb_module(nb::module_& module) {
    nb::class_<cricodecs::acb::AcbContainer>(module, "Acb")
        .def_static(
            "load",
            &load_acb_any,
            nb::arg("source"),
            nb::arg("encoding") = nb::none()
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data, nb::object encoding) {
                auto owned = copy_python_bytes(data);
                return unwrap_expected(cricodecs::acb::AcbContainer::load(
                    std::move(owned),
                    python_encoding_options_from_object(encoding)
                ));
            },
            nb::arg("data"),
            nb::arg("encoding") = nb::none(),
            "Load an ACB from raw bytes."
        )
        .def_prop_ro("source_path", [](const cricodecs::acb::AcbContainer& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("name", [](const cricodecs::acb::AcbContainer& self) {
            return std::string(self.name());
        })
        .def_prop_ro("waveform_count", &cricodecs::acb::AcbContainer::waveform_count)
        .def_prop_ro("has_embedded_awb", &cricodecs::acb::AcbContainer::has_embedded_awb)
        .def_prop_ro("companion_awb_path", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            const auto path = self.companion_awb_path();
            return path ? nb::cast(path->generic_string()) : nb::none();
        })
        .def_prop_ro("has_aac_waveforms", &cricodecs::acb::AcbContainer::has_aac_waveforms)
        .def("info", [](const cricodecs::acb::AcbContainer& self) {
            nb::object info = simple_namespace();
            info.attr("table_name") = std::string(self.header_table().table_name());
            info.attr("row_count") = self.header_table().row_count();
            info.attr("column_count") = self.header_table().column_count();
            info.attr("source_path") = path_or_none(self.source_path());
            info.attr("name") = std::string(self.name());
            info.attr("waveform_count") = self.waveform_count();
            nb::list waveforms;
            for (uint32_t index = 0; index < self.waveform_count(); ++index) {
                nb::object waveform = simple_namespace();
                waveform.attr("index") = index;
                waveform.attr("name") = std::string(self.waveform_name(index));
                waveform.attr("name_raw") = string_to_python_bytes(self.waveform_name_raw(index));
                waveform.attr("filename") = self.waveform_filename(index, true);
                waveforms.append(waveform);
            }
            info.attr("waveforms") = waveforms;
            info.attr("has_embedded_awb") = self.has_embedded_awb();
            const auto companion = self.companion_awb_path();
            info.attr("companion_awb_path") = companion.has_value() ? nb::cast(companion->generic_string()) : nb::none();
            info.attr("has_aac_waveforms") = self.has_aac_waveforms();
            if (self.has_embedded_awb() || companion) {
                auto awb = unwrap_expected(self.load_awb());
                nb::object awb_info = simple_namespace();
                awb_info.attr("source_path") = path_or_none(awb.source_path());
                awb_info.attr("file_count") = awb.file_count();
                awb_info.attr("version") = awb.version();
                awb_info.attr("offset_size") = awb.offset_size();
                awb_info.attr("id_size") = awb.id_size();
                awb_info.attr("alignment") = awb.alignment();
                awb_info.attr("subkey") = awb.subkey();
                info.attr("awb_info") = awb_info;
            } else {
                info.attr("awb_info") = nb::none();
            }
            return info;
        })
        .def("waveform", [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
            static_cast<void>(checked_container(self, index));
            nb::object waveform = simple_namespace();
            waveform.attr("index") = index;
            waveform.attr("name") = std::string(self.waveform_name(index));
            waveform.attr("name_raw") = string_to_python_bytes(self.waveform_name_raw(index));
            waveform.attr("filename") = self.waveform_filename(index, true);
            return waveform;
        }, nb::arg("index"))
        .def(
            "waveform_name",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
                const auto& container = checked_container(self, index);
                return std::string(container.waveform_name(index));
            },
            nb::arg("index")
        )
        .def(
            "waveform_name_raw",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
                const auto& container = checked_container(self, index);
                return string_to_python_bytes(container.waveform_name_raw(index));
            },
            nb::arg("index")
        )
        .def(
            "waveform_filename",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, bool include_index_prefix) {
                const auto& container = checked_container(self, index);
                return container.waveform_filename(index, include_index_prefix);
            },
            nb::arg("index"),
            nb::arg("include_index_prefix") = true
        )
        .def(
            "extract_waveform_data",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "waveform_bytes",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "extract_waveform_stream_data",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_stream_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "probe_waveform_aac_encryption",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t keycode) {
                return unwrap_expected(self.probe_waveform_aac_encryption(index, keycode));
            },
            nb::arg("index"),
            nb::arg("keycode")
        )
        .def("recover_aac_key", [](const cricodecs::acb::AcbContainer& self) {
            return unwrap_expected(self.recover_aac_key());
        })
        .def("embedded_awb_bytes", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            const auto bytes = self.embedded_awb();
            if (!bytes) {
                return nb::none();
            }
            return to_python_bytes(*bytes);
        })
        .def("load_awb", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            if (!self.has_embedded_awb() && !self.companion_awb_path()) {
                return nb::none();
            }
            return nb::cast(unwrap_expected(self.load_awb()));
        })
        .def(
            "extract_file",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, const nb::object& output_path, uint64_t aac_keycode) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path"), aac_keycode));
            },
            nb::arg("index"),
            nb::arg("output_path"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "extract",
            [](const cricodecs::acb::AcbContainer& self, const nb::object& output_dir, uint64_t aac_keycode) {
                unwrap_expected(self.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
            },
            nb::arg("output_dir"),
            nb::arg("aac_keycode") = 0
        );

    install_attr_repr(module, "Acb", {"source_path", "name", "waveform_count", "has_embedded_awb", "companion_awb_path", "has_aac_waveforms"});

    module.def(
        "load",
        &load_acb_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "extract",
        [](const cricodecs::acb::AcbContainer& acb, const nb::object& output_dir, uint64_t aac_keycode) {
            unwrap_expected(acb.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("aac_keycode") = static_cast<uint64_t>(0)
    );
    module.def(
        "extract",
        [](const nb::object& source, const nb::object& output_dir, uint64_t aac_keycode, nb::object encoding) {
            auto acb = load_acb_any(source, encoding);
            unwrap_expected(acb.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("aac_keycode") = static_cast<uint64_t>(0),
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python
