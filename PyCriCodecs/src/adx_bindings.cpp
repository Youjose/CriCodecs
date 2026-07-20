#include "binding_helpers.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/adx/adx_codec.hpp"
#include "../../CriCodecs/src/adx/adx_key_recovery.hpp"
#include "../../CriCodecs/src/key_recovery/key_recovery.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] nb::list loops_list(const cricodecs::adx::Adx& adx) {
    nb::list loops;
    for (const auto& loop : adx.loops()) {
        loops.append(loop);
    }
    return loops;
}

[[nodiscard]] std::vector<cricodecs::adx::AdxLoop> native_loops_from_python(const nb::list& loops) {
    std::vector<cricodecs::adx::AdxLoop> native_loops;
    native_loops.reserve(static_cast<size_t>(PyList_Size(loops.ptr())));
    for (auto item : loops) {
        native_loops.push_back(nb::cast<cricodecs::adx::AdxLoop>(item));
    }
    return native_loops;
}

[[nodiscard]] cricodecs::adx::Adx load_adx_any(const nb::object& source) {
    return load_path_or_borrowed_source(source, [](const std::filesystem::path& path, std::span<const uint8_t> data) {
        if (!path.empty()) {
            return unwrap_expected(cricodecs::adx::Adx::load(path));
        }
        return unwrap_expected(cricodecs::adx::Adx::load(data));
    });
}

[[nodiscard]] cricodecs::adx::AdxEncodeConfig default_adx_config_for_wav(const cricodecs::wav::WavContainer& wav) {
    cricodecs::adx::AdxEncodeConfig config;
    config.sample_rate = wav.sample_rate();
    config.channels = static_cast<uint8_t>(wav.channels());
    return config;
}

[[nodiscard]] cricodecs::adx::KeyRecoveryResult recover_adx_python(
    const nb::object& source,
    bool same_base_key)
{
    auto bytes = copy_python_recovery_sources(source, "ADX key recovery");
    std::vector<cricodecs::adx::AdxRecoverySource> sources;
    sources.reserve(bytes.size());
    for (const auto& data : bytes) sources.push_back({data});
    return unwrap_expected(cricodecs::adx::recover_key(
        sources,
        same_base_key
            ? cricodecs::KeyRecoveryMode::SharedBaseKey
            : cricodecs::KeyRecoveryMode::Independent));
}

[[nodiscard]] nb::bytes decode_adx_to_wav(cricodecs::adx::Adx& adx) {
    auto decoded = unwrap_expected(adx.decode());
    std::vector<cricodecs::wav::SampleLoop> loops;
    if (decoded.has_loops) {
        loops.push_back(cricodecs::wav::SampleLoop{
            .cue_point_id = 0,
            .type = 0,
            .start = decoded.loop_start,
            .end = decoded.loop_end,
            .fraction = 0,
            .play_count = 0,
        });
    }
    return pcm16_to_wav_python_bytes(
        decoded.pcm_data,
        decoded.sample_rate,
        decoded.channels,
        loops
    );
}

void apply_adx_key(cricodecs::adx::Adx& adx, const nb::object& key, uint16_t subkey) {
    if (key.is_none()) {
        return;
    }
    if (PyBytes_Check(key.ptr()) || PyByteArray_Check(key.ptr()) || PyMemoryView_Check(key.ptr())) {
        auto borrowed = borrow_python_source(key);
        const auto bytes = borrowed.as_span();
        if (bytes.size() == 6) {
            const auto start = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8u) | bytes[1]);
            const auto mult = static_cast<uint16_t>((static_cast<uint16_t>(bytes[2]) << 8u) | bytes[3]);
            const auto add = static_cast<uint16_t>((static_cast<uint16_t>(bytes[4]) << 8u) | bytes[5]);
            adx.set_ahx_key(start, mult, add);
            return;
        }
        adx.set_key_type8(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return;
    }
    if (PySequence_Check(key.ptr()) && !PyUnicode_Check(key.ptr())) {
        if (PySequence_Size(key.ptr()) != 3) {
            raise_value_error("ADX/AHX tuple/list key must contain exactly three integers");
        }
        const auto start = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 0)));
        const auto mult = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 1)));
        const auto add = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 2)));
        adx.set_ahx_key(start, mult, add);
        return;
    }
    if (PyUnicode_Check(key.ptr())) {
        const auto text = nb::cast<std::string>(key);
        if (text.find("\xEF\xBF\xBD") != std::string::npos) {
            raise_value_error("ADX/AHX key string contains Unicode replacement characters; pass raw key bytes instead");
        }
        adx.set_key_type8(text);
        return;
    }
    if (PyLong_Check(key.ptr())) {
        adx.set_key_type9(nb::cast<uint64_t>(key), subkey);
        return;
    }
    raise_type_error("ADX key must be None, raw bytes, tuple/list triplet, string, or integer keycode");
}

} // namespace

void bind_adx_module(nb::module_& module) {
    nb::class_<cricodecs::adx::AdxKeyState>(module, "AdxKey")
        .def(nb::init<>())
        .def_rw("start", &cricodecs::adx::AdxKeyState::xor_value)
        .def_rw("mult", &cricodecs::adx::AdxKeyState::mult)
        .def_rw("add", &cricodecs::adx::AdxKeyState::add);

    nb::class_<cricodecs::adx::KeyCandidate>(module, "KeyCandidate")
        .def_ro("key", &cricodecs::adx::KeyCandidate::key)
        .def_ro("score", &cricodecs::adx::KeyCandidate::score)
        .def_ro("source_count", &cricodecs::adx::KeyCandidate::source_count)
        .def_ro("evidence_count", &cricodecs::adx::KeyCandidate::evidence_count)
        .def_ro("evidence_frames", &cricodecs::adx::KeyCandidate::evidence_frames)
        .def_ro("canonical_type9_code", &cricodecs::adx::KeyCandidate::canonical_type9_code);

    nb::class_<cricodecs::adx::KeyRecoveryResult>(module, "KeyRecoveryResult")
        .def_ro("candidates", &cricodecs::adx::KeyRecoveryResult::candidates)
        .def_ro("source_count", &cricodecs::adx::KeyRecoveryResult::source_count)
        .def_ro("evidence_count", &cricodecs::adx::KeyRecoveryResult::evidence_count);

    nb::class_<cricodecs::adx::AdxLoop>(module, "AdxLoop")
        .def(nb::init<>())
        .def_rw("index", &cricodecs::adx::AdxLoop::index)
        .def_rw("type", &cricodecs::adx::AdxLoop::type)
        .def_rw("start_sample", &cricodecs::adx::AdxLoop::start_sample)
        .def_rw("start_byte", &cricodecs::adx::AdxLoop::start_byte)
        .def_rw("end_sample", &cricodecs::adx::AdxLoop::end_sample)
        .def_rw("end_byte", &cricodecs::adx::AdxLoop::end_byte);

    nb::class_<cricodecs::adx::AdxHeader>(module, "AdxHeader")
        .def_ro("signature", &cricodecs::adx::AdxHeader::signature)
        .def_ro("data_offset", &cricodecs::adx::AdxHeader::data_offset)
        .def_ro("encoding_mode", &cricodecs::adx::AdxHeader::encoding_mode)
        .def_ro("block_size", &cricodecs::adx::AdxHeader::block_size)
        .def_ro("bit_depth", &cricodecs::adx::AdxHeader::bit_depth)
        .def_ro("channels", &cricodecs::adx::AdxHeader::channels)
        .def_ro("sample_rate", &cricodecs::adx::AdxHeader::sample_rate)
        .def_ro("sample_count", &cricodecs::adx::AdxHeader::sample_count)
        .def_ro("highpass_freq", &cricodecs::adx::AdxHeader::highpass_freq)
        .def_ro("version", &cricodecs::adx::AdxHeader::version)
        .def_ro("flags", &cricodecs::adx::AdxHeader::flags);

    nb::class_<cricodecs::adx::AdxEncodeConfig>(module, "AdxEncodeConfig")
        .def(nb::init<>())
        .def_rw("sample_rate", &cricodecs::adx::AdxEncodeConfig::sample_rate)
        .def_rw("channels", &cricodecs::adx::AdxEncodeConfig::channels)
        .def_rw("bit_depth", &cricodecs::adx::AdxEncodeConfig::bit_depth)
        .def_rw("block_size", &cricodecs::adx::AdxEncodeConfig::block_size)
        .def_rw("encoding_mode", &cricodecs::adx::AdxEncodeConfig::encoding_mode)
        .def_rw("highpass_freq", &cricodecs::adx::AdxEncodeConfig::highpass_freq)
        .def_rw("filter_id", &cricodecs::adx::AdxEncodeConfig::filter_id)
        .def_rw("version", &cricodecs::adx::AdxEncodeConfig::version)
        .def_rw("encryption_type", &cricodecs::adx::AdxEncodeConfig::encryption_type)
        .def_rw("delete_samples_after_loop_end", &cricodecs::adx::AdxEncodeConfig::delete_samples_after_loop_end)
        .def_rw("key_string", &cricodecs::adx::AdxEncodeConfig::key_string)
        .def_rw("key64", &cricodecs::adx::AdxEncodeConfig::key64)
        .def_rw("subkey", &cricodecs::adx::AdxEncodeConfig::subkey);

    nb::class_<cricodecs::adx::Adx>(module, "Adx")
        .def_static("load", &load_adx_any, nb::arg("source"))
        .def_static(
            "load_bytes",
            [](const nb::bytes& data) {
                return unwrap_expected(cricodecs::adx::Adx::load(as_byte_span(borrow_python_bytes(data))));
            },
            nb::arg("data")
        )
        .def_prop_ro("source_path", [](const cricodecs::adx::Adx& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro(
            "header",
            [](const cricodecs::adx::Adx& self) -> const cricodecs::adx::AdxHeader& {
                return self.header();
            },
            nb::rv_policy::reference_internal
        )
        .def_prop_ro("has_loops", [](const cricodecs::adx::Adx& self) {
            return self.has_loops();
        })
        .def_prop_ro("loops", [](const cricodecs::adx::Adx& self) {
            return loops_list(self);
        })
        .def_prop_ro("is_encrypted", [](const cricodecs::adx::Adx& self) {
            return self.is_encrypted();
        })
        .def_prop_ro("is_ahx", [](const cricodecs::adx::Adx& self) {
            return self.is_ahx();
        })
        .def("info", [](const cricodecs::adx::Adx& self) {
            nb::object info = simple_namespace();
            info.attr("header") = self.header();
            info.attr("has_loops") = self.has_loops();
            info.attr("loops") = loops_list(self);
            info.attr("is_encrypted") = self.is_encrypted();
            info.attr("is_ahx") = self.is_ahx();
            info.attr("source_path") = path_or_none(self.source_path());
            return info;
        })
        .def("set_key_type8", [](cricodecs::adx::Adx& self, const std::string& key) {
            self.set_key_type8(key);
        }, nb::arg("key"))
        .def("_set_key_type8_bytes", [](cricodecs::adx::Adx& self, const nb::bytes& key) {
            self.set_key_type8(borrow_python_bytes(key));
        }, nb::arg("key"))
        .def("set_key_type9", [](cricodecs::adx::Adx& self, uint64_t key, uint16_t subkey) {
            self.set_key_type9(key, subkey);
        }, nb::arg("key"), nb::arg("subkey") = 0)
        .def("set_ahx_key", [](cricodecs::adx::Adx& self, uint16_t start, uint16_t mult, uint16_t add) {
            self.set_ahx_key(start, mult, add);
        }, nb::arg("start"), nb::arg("mult"), nb::arg("add"))
        .def("decode", &decode_adx_to_wav)
        .def("decrypt", [](const cricodecs::adx::Adx& self) {
            return to_python_bytes(unwrap_expected(self.decrypt()));
        })
        .def("encode", [](const cricodecs::adx::Adx& self) {
            return to_python_bytes(unwrap_expected(self.rebuild()));
        })
        .def("encode", [](cricodecs::adx::Adx& self, const cricodecs::adx::AdxEncodeConfig& config, const nb::list& loops) {
            return to_python_bytes(unwrap_expected(self.encode(config, native_loops_from_python(loops))));
        }, nb::arg("config"), nb::arg("loops") = nb::list())
        .def("rebuild", [](const cricodecs::adx::Adx& self) {
            return to_python_bytes(unwrap_expected(self.rebuild()));
        })
        .def("recover_key", [](const cricodecs::adx::Adx& self) {
            const auto data = unwrap_expected(self.rebuild());
            const std::array sources{cricodecs::adx::AdxRecoverySource{data}};
            return unwrap_expected(cricodecs::adx::recover_key(sources));
        })
        .def("_encode_with_config", [](cricodecs::adx::Adx& self, const cricodecs::adx::AdxEncodeConfig& config, const nb::list& loops) {
            std::vector<cricodecs::adx::AdxLoop> native_loops;
            native_loops.reserve(static_cast<size_t>(PyList_Size(loops.ptr())));
            for (auto item : loops) {
                native_loops.push_back(nb::cast<cricodecs::adx::AdxLoop>(item));
            }
            auto encoded = unwrap_expected(self.encode(config, native_loops));
            return to_python_bytes(encoded);
        }, nb::arg("config"), nb::arg("loops") = nb::list());

    install_attr_repr(module, "AdxLoop", {"index", "type", "start_sample", "start_byte", "end_sample", "end_byte"});
    install_attr_repr(module, "AdxKey", {"start", "mult", "add"});
    install_attr_repr(module, "KeyCandidate", {"key", "score", "source_count", "evidence_count", "canonical_type9_code"});
    install_attr_repr(module, "KeyRecoveryResult", {"candidates", "source_count", "evidence_count"});
    install_attr_repr(module, "AdxHeader", {"signature", "data_offset", "encoding_mode", "block_size", "bit_depth", "channels", "sample_rate", "sample_count", "highpass_freq", "version", "flags"});
    install_attr_repr(module, "AdxEncodeConfig", {"sample_rate", "channels", "bit_depth", "block_size", "encoding_mode", "highpass_freq", "filter_id", "version", "encryption_type", "delete_samples_after_loop_end", "key_string", "key64", "subkey"});
    install_attr_repr(module, "Adx", {"source_path", "header", "has_loops", "loops", "is_encrypted", "is_ahx"});

    module.def("encode",
        [](const cricodecs::wav::WavContainer& wav) {
            auto encoded = unwrap_expected(
                cricodecs::adx::AdxEncoder::encode(wav, default_adx_config_for_wav(wav), {})
            );
            return to_python_bytes(encoded);
        },
        nb::arg("wav")
    );

    module.def("encode",
        [](const cricodecs::wav::WavContainer& wav, const cricodecs::adx::AdxEncodeConfig& config, const nb::list& loops) {
            const auto native_loops = native_loops_from_python(loops);
            auto encoded = unwrap_expected(
                cricodecs::adx::AdxEncoder::encode(wav, config, native_loops)
            );
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config"),
        nb::arg("loops") = nb::list()
    );

    module.def("encode",
        [](const nb::bytes& wav, const cricodecs::adx::AdxEncodeConfig& config, const nb::list& loops) {
            auto source = wav_pcm16_from_python_bytes(wav, "ADX");
            auto effective_config = config;
            effective_config.sample_rate = source.sample_rate;
            effective_config.channels = static_cast<uint8_t>(source.channels);
            std::vector<cricodecs::adx::AdxLoop> native_loops;
            if (PyList_Size(loops.ptr()) > 0) {
                native_loops = native_loops_from_python(loops);
            } else {
                native_loops.reserve(source.loops.size());
                for (const auto& loop : source.loops) {
                    native_loops.push_back(cricodecs::adx::AdxLoop{
                        .index = static_cast<uint16_t>(loop.cue_point_id),
                        .type = static_cast<uint16_t>(loop.type),
                        .start_sample = loop.start,
                        .start_byte = 0,
                        .end_sample = loop.end,
                        .end_byte = 0,
                    });
                }
            }
            auto encoded = unwrap_expected(
                cricodecs::adx::AdxEncoder::encode(source.samples, effective_config, native_loops)
            );
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config"),
        nb::arg("loops") = nb::list()
    );

    module.def("load", &load_adx_any, nb::arg("source"));
    module.def(
        "recover_key",
        &recover_adx_python,
        nb::arg("source"),
        nb::arg("same_base_key") = true,
        "Recover up to ten ranked ADX key-triplet candidates from one or more sources."
    );
    module.def("decode", [](const nb::object& source, const nb::object& key, uint16_t subkey) {
        auto adx = load_adx_any(source);
        apply_adx_key(adx, key, subkey);
        return decode_adx_to_wav(adx);
    }, nb::arg("source"), nb::arg("key") = nb::none(), nb::arg("subkey") = static_cast<uint16_t>(0));
}

} // namespace cricodecs::python
