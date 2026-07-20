#include "binding_helpers.hpp"

#include <filesystem>
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

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

[[nodiscard]] cricodecs::awb::KeyRecoveryResult recover_awb_aac_python(
    const nb::object& source,
    bool same_base_key)
{
    std::vector<cricodecs::awb::AwbContainer> banks;
    if (PyList_Check(source.ptr()) || PyTuple_Check(source.ptr())) {
        const auto sequence = nb::borrow<nb::sequence>(source);
        banks.reserve(nb::len(sequence));
        for (const auto item : sequence) banks.push_back(load_awb_any(nb::borrow<nb::object>(item)));
    } else {
        banks.push_back(load_awb_any(source));
    }
    if (banks.empty()) raise_value_error("AWB AAC key recovery requires at least one source");

    struct Aggregate {
        cricodecs::awb::KeyCandidate candidate;
        size_t bank_count = 0;
        double score_sum = 0.0;
    };
    std::map<uint64_t, Aggregate> aggregated;
    size_t evidence_count = 0;
    for (const auto& bank : banks) {
        auto recovered = unwrap_expected(bank.recover_aac_key());
        evidence_count += recovered.evidence_count;
        for (const auto& candidate : recovered.candidates) {
            auto& aggregate = aggregated[candidate.key];
            if (aggregate.bank_count == 0u) aggregate.candidate = candidate;
            ++aggregate.bank_count;
            aggregate.score_sum += candidate.score;
            aggregate.candidate.validated_sources +=
                aggregate.bank_count == 1u ? 0u : candidate.validated_sources;
            aggregate.candidate.source_count +=
                aggregate.bank_count == 1u ? 0u : candidate.source_count;
        }
    }

    std::vector<cricodecs::awb::KeyCandidate> candidates;
    for (auto& [_, aggregate] : aggregated) {
        if (same_base_key && aggregate.bank_count != banks.size()) continue;
        aggregate.candidate.score = static_cast<float>(aggregate.score_sum / aggregate.bank_count);
        candidates.push_back(aggregate.candidate);
    }
    if (candidates.empty()) {
        raise_value_error("AWB AAC key recovery found no candidate shared by every supplied bank");
    }
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        if (left.validated_sources != right.validated_sources) {
            return left.validated_sources > right.validated_sources;
        }
        if (left.score != right.score) return left.score > right.score;
        return left.key < right.key;
    });
    if (candidates.size() > cricodecs::MaxKeyRecoveryCandidates) {
        candidates.resize(cricodecs::MaxKeyRecoveryCandidates);
    }
    return {
        .candidates = std::move(candidates),
        .source_count = banks.size(),
        .evidence_count = evidence_count,
    };
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

    nb::class_<cricodecs::awb::KeyCandidate>(module, "KeyCandidate")
        .def_ro("key", &cricodecs::awb::KeyCandidate::key)
        .def_ro("score", &cricodecs::awb::KeyCandidate::score)
        .def_ro("source_count", &cricodecs::awb::KeyCandidate::source_count)
        .def_ro("validated_sources", &cricodecs::awb::KeyCandidate::validated_sources)
        .def_ro("candidate_count", &cricodecs::awb::KeyCandidate::candidate_count);

    nb::class_<cricodecs::awb::KeyRecoveryResult>(module, "KeyRecoveryResult")
        .def_ro("candidates", &cricodecs::awb::KeyRecoveryResult::candidates)
        .def_ro("source_count", &cricodecs::awb::KeyRecoveryResult::source_count)
        .def_ro("evidence_count", &cricodecs::awb::KeyRecoveryResult::evidence_count);

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
        .def_static("load", &load_awb_any, nb::arg("source"))
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
            return path_or_none(self.source_path());
        })
        .def_prop_ro("file_count", &cricodecs::awb::AwbContainer::file_count)
        .def_prop_rw("version", &cricodecs::awb::AwbContainer::version,
            [](cricodecs::awb::AwbContainer& self, uint8_t value) { unwrap_expected(self.set_version(value)); })
        .def_prop_rw("offset_size", &cricodecs::awb::AwbContainer::offset_size,
            [](cricodecs::awb::AwbContainer& self, uint8_t value) { unwrap_expected(self.set_offset_size(value)); })
        .def_prop_rw("id_size", &cricodecs::awb::AwbContainer::id_size,
            [](cricodecs::awb::AwbContainer& self, uint8_t value) { unwrap_expected(self.set_id_size(value)); })
        .def_prop_rw("alignment", &cricodecs::awb::AwbContainer::alignment,
            [](cricodecs::awb::AwbContainer& self, uint16_t value) { unwrap_expected(self.set_alignment(value)); })
        .def_prop_rw("subkey", &cricodecs::awb::AwbContainer::subkey,
            [](cricodecs::awb::AwbContainer& self, uint16_t value) { unwrap_expected(self.set_subkey(value)); })
        .def_prop_ro("is_materialized", &cricodecs::awb::AwbContainer::is_materialized)
        .def_prop_ro("entries", [](const cricodecs::awb::AwbContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def("info", [](const cricodecs::awb::AwbContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = path_or_none(self.source_path());
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
            [](const cricodecs::awb::AwbContainer& self, const nb::object& output_dir) {
                unwrap_expected(self.extract(require_python_path(output_dir, "output_dir")));
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
            [](cricodecs::awb::AwbContainer& self, const nb::object& local_path, std::optional<uint64_t> wave_id) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    require_python_path(local_path, "local_path"),
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
            [](cricodecs::awb::AwbContainer& self, uint32_t index, const nb::object& local_path) {
                auto file_bytes = unwrap_expected(cricodecs::io::read_file_bytes(
                    require_python_path(local_path, "local_path"),
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
            "move_file",
            [](cricodecs::awb::AwbContainer& self, uint32_t from_index, uint32_t to_index) {
                unwrap_expected(self.move_file(from_index, to_index));
            },
            nb::arg("from_index"),
            nb::arg("to_index")
        )
        .def("materialize", [](cricodecs::awb::AwbContainer& self) {
            unwrap_expected(self.materialize());
        })
        .def(
            "probe_aac_encryption",
            [](const cricodecs::awb::AwbContainer& self, uint32_t index, uint64_t keycode) {
                return unwrap_expected(self.probe_aac_encryption(index, keycode));
            },
            nb::arg("index"),
            nb::arg("keycode")
        )
        .def("recover_aac_key", [](const cricodecs::awb::AwbContainer& self) {
            return unwrap_expected(self.recover_aac_key());
        });

    install_attr_repr(module, "AwbEntry", {"wave_id", "offset", "size"});
    install_attr_repr(module, "KeyCandidate", {"key", "score", "source_count", "validated_sources", "candidate_count"});
    install_attr_repr(module, "KeyRecoveryResult", {"candidates", "source_count", "evidence_count"});
    install_attr_repr(module, "Awb", {"source_path", "file_count", "version", "offset_size", "id_size", "alignment", "subkey", "entries"});

    module.def("load", &load_awb_any, nb::arg("source"));
    module.def(
        "recover_aac_key",
        &recover_awb_aac_python,
        nb::arg("source"),
        nb::arg("same_base_key") = true,
        "Recover up to ten ranked AAC key candidates from one or more AWB banks."
    );
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
    module.def("extract", [](const nb::object& source, const nb::object& output_dir) {
        auto awb = load_awb_any(source);
        unwrap_expected(awb.extract(require_python_path(output_dir, "output_dir")));
    }, nb::arg("source"), nb::arg("output_dir"));
}

} // namespace cricodecs::python
