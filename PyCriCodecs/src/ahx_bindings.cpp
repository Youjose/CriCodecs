#include "binding_helpers.hpp"

#include <algorithm>
#include <array>

#include <nanobind/stl/array.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/ahx/ahx_codec.hpp"
#include "../../CriCodecs/src/ahx/ahx_key_recovery.hpp"
#include "../../CriCodecs/src/key_recovery/key_recovery.hpp"
#include "../../CriCodecs/src/adx/adx_crypto.hpp"
#include "../../CriCodecs/src/adx/adx_codec.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] nb::list pattern_to_list(const cricodecs::ahx::AhxBitAllocationPattern& pattern) {
    nb::list values;
    for (const auto value : pattern) {
        values.append(value);
    }
    return values;
}

[[nodiscard]] cricodecs::ahx::AhxBitAllocationPattern pattern_from_object(
    const nb::object& value,
    std::string_view context
) {
    if (value.is_none()) {
        return cricodecs::ahx::default_bit_allocation_pattern();
    }
    if (!PySequence_Check(value.ptr())) {
        raise_value_error(std::string(context) + " must be a sequence of 32 integers");
    }

    const auto count = static_cast<size_t>(PySequence_Size(value.ptr()));
    if (count != 32) {
        raise_value_error(std::string(context) + " must contain exactly 32 integers");
    }

    cricodecs::ahx::AhxBitAllocationPattern pattern{};
    for (size_t index = 0; index < pattern.size(); ++index) {
        auto item = nb::steal<nb::object>(PySequence_GetItem(value.ptr(), static_cast<Py_ssize_t>(index)));
        const int raw = nb::cast<int>(item);
        if (raw < 0 || raw > 0xFF) {
            raise_value_error(std::string(context) + " entries must be in the range 0..255");
        }
        pattern[index] = static_cast<uint8_t>(raw);
    }
    return pattern;
}

[[nodiscard]] cricodecs::ahx::AhxKey key_from_raw_triplet(std::span<const uint8_t> bytes) {
    if (bytes.size() != 6) {
        raise_value_error("AHX raw key triplet must be exactly 6 bytes");
    }
    return cricodecs::ahx::AhxKey{
        .start = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8u) | bytes[1]),
        .mult = static_cast<uint16_t>((static_cast<uint16_t>(bytes[2]) << 8u) | bytes[3]),
        .add = static_cast<uint16_t>((static_cast<uint16_t>(bytes[4]) << 8u) | bytes[5]),
    };
}

[[nodiscard]] cricodecs::ahx::AhxKey ahx_key_from_adx_state(cricodecs::adx::AdxKeyState state) {
    return cricodecs::ahx::AhxKey{
        .start = state.xor_value,
        .mult = state.mult,
        .add = state.add,
    };
}

[[nodiscard]] cricodecs::ahx::AhxKey ahx_key_from_object(const nb::object& key, uint16_t subkey = 0) {
    if (key.is_none()) {
        return {};
    }
    if (nb::isinstance<cricodecs::ahx::AhxKey>(key)) {
        return nb::cast<cricodecs::ahx::AhxKey>(key);
    }
    if (PyBytes_Check(key.ptr()) || PyByteArray_Check(key.ptr()) || PyMemoryView_Check(key.ptr())) {
        auto borrowed = borrow_python_source(key);
        return key_from_raw_triplet(borrowed.as_span());
    }
    if (PySequence_Check(key.ptr()) && !PyUnicode_Check(key.ptr())) {
        if (PySequence_Size(key.ptr()) != 3) {
            raise_value_error("AHX tuple/list key must contain exactly three integers");
        }
        return cricodecs::ahx::AhxKey{
            .start = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 0))),
            .mult = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 1))),
            .add = nb::cast<uint16_t>(nb::steal<nb::object>(PySequence_GetItem(key.ptr(), 2))),
        };
    }
    if (PyUnicode_Check(key.ptr())) {
        const auto text = nb::cast<std::string>(key);
        if (text.find("\xEF\xBF\xBD") != std::string::npos) {
            raise_value_error("AHX key string contains Unicode replacement characters; pass raw key bytes instead");
        }
        auto state = cricodecs::adx::key8_derive(text);
        if (subkey != 0) {
            state = cricodecs::adx::key9_derive(0, subkey);
        }
        return ahx_key_from_adx_state(state);
    }
    if (PyLong_Check(key.ptr())) {
        return ahx_key_from_adx_state(cricodecs::adx::key9_derive(nb::cast<uint64_t>(key), subkey));
    }
    raise_type_error("AHX key must be None, AhxKey, raw 6-byte triplet, tuple/list triplet, string, or integer keycode");
}

[[nodiscard]] cricodecs::adx::Adx load_adx_for_ahx(const nb::object& source) {
    return load_path_or_borrowed_source(source, [](const std::filesystem::path& path, std::span<const uint8_t> data) {
        if (!path.empty()) {
            return unwrap_expected(cricodecs::adx::Adx::load(path));
        }
        return unwrap_expected(cricodecs::adx::Adx::load(data));
    });
}

[[nodiscard]] cricodecs::ahx::AhxDecodeConfig ahx_decode_config_from_adx(const cricodecs::adx::Adx& adx) {
    const auto& header = adx.header();
    cricodecs::ahx::AhxDecodeConfig config;
    config.encoding_mode = header.encoding_mode;
    config.sample_rate = header.sample_rate;
    config.sample_count = header.sample_count;
    config.channels = header.channels;
    config.encryption_type = header.flags;
    config.start_offset = static_cast<uint16_t>(header.data_offset + 4);
    return config;
}

[[nodiscard]] cricodecs::wav::WavContainer load_wav_for_ahx(const nb::object& source) {
    cricodecs::wav::WavContainer wav;
    if (auto path = python_text_path(source)) {
        unwrap_expected(wav.load(std::filesystem::path(*path)));
        return wav;
    }
    auto borrowed = borrow_python_source(source);
    unwrap_expected(wav.load(borrowed.as_span()));
    return wav;
}

[[nodiscard]] cricodecs::ahx::KeyRecoveryResult recover_ahx_python(
    const nb::object& source,
    bool same_base_key)
{
    auto bytes = copy_python_recovery_sources(source, "AHX key recovery");
    std::vector<cricodecs::ahx::AhxRecoverySource> sources;
    sources.reserve(bytes.size());
    for (const auto& data : bytes) sources.push_back({data});
    return unwrap_expected(cricodecs::ahx::recover_key(
        sources,
        same_base_key
            ? cricodecs::KeyRecoveryMode::SharedBaseKey
            : cricodecs::KeyRecoveryMode::Independent));
}

} // namespace

void bind_ahx_module(nb::module_& module) {
    nb::enum_<cricodecs::ahx::AhxBitAllocationPreset>(module, "AhxBitAllocationPreset")
        .value("DEFAULT_PATTERN", cricodecs::ahx::AhxBitAllocationPreset::default_pattern)
        .value("PRESET_22050", cricodecs::ahx::AhxBitAllocationPreset::preset_22050)
        .value("PRESET_24000", cricodecs::ahx::AhxBitAllocationPreset::preset_24000)
        .value("PRESET_44100", cricodecs::ahx::AhxBitAllocationPreset::preset_44100)
        .value("PRESET_48000", cricodecs::ahx::AhxBitAllocationPreset::preset_48000);

    nb::class_<cricodecs::ahx::AhxKey>(module, "AhxKey")
        .def(nb::init<>())
        .def_rw("start", &cricodecs::ahx::AhxKey::start)
        .def_rw("mult", &cricodecs::ahx::AhxKey::mult)
        .def_rw("add", &cricodecs::ahx::AhxKey::add)
        .def("empty", &cricodecs::ahx::AhxKey::empty);

    nb::class_<cricodecs::ahx::KeyCandidate>(module, "KeyCandidate")
        .def_ro("key", &cricodecs::ahx::KeyCandidate::key)
        .def_ro("score", &cricodecs::ahx::KeyCandidate::score)
        .def_ro("source_count", &cricodecs::ahx::KeyCandidate::source_count)
        .def_ro("evidence_count", &cricodecs::ahx::KeyCandidate::evidence_count)
        .def_ro("evidence_frames", &cricodecs::ahx::KeyCandidate::evidence_frames)
        .def_ro("candidate_counts", &cricodecs::ahx::KeyCandidate::candidate_counts)
        .def_ro("canonical_type9_code", &cricodecs::ahx::KeyCandidate::canonical_type9_code);

    nb::class_<cricodecs::ahx::KeyRecoveryResult>(module, "KeyRecoveryResult")
        .def_ro("candidates", &cricodecs::ahx::KeyRecoveryResult::candidates)
        .def_ro("source_count", &cricodecs::ahx::KeyRecoveryResult::source_count)
        .def_ro("evidence_count", &cricodecs::ahx::KeyRecoveryResult::evidence_count);

    nb::class_<cricodecs::ahx::AhxDecodeConfig>(module, "AhxDecodeConfig")
        .def(nb::init<>())
        .def_rw("encoding_mode", &cricodecs::ahx::AhxDecodeConfig::encoding_mode)
        .def_rw("sample_rate", &cricodecs::ahx::AhxDecodeConfig::sample_rate)
        .def_rw("sample_count", &cricodecs::ahx::AhxDecodeConfig::sample_count)
        .def_rw("channels", &cricodecs::ahx::AhxDecodeConfig::channels)
        .def_rw("encryption_type", &cricodecs::ahx::AhxDecodeConfig::encryption_type)
        .def_rw("start_offset", &cricodecs::ahx::AhxDecodeConfig::start_offset)
        .def_rw("key", &cricodecs::ahx::AhxDecodeConfig::key);

    nb::class_<cricodecs::ahx::AhxEncodeConfig>(module, "AhxEncodeConfig")
        .def(nb::init<>())
        .def_rw("encoding_mode", &cricodecs::ahx::AhxEncodeConfig::encoding_mode)
        .def_rw("sample_rate", &cricodecs::ahx::AhxEncodeConfig::sample_rate)
        .def_rw("channels", &cricodecs::ahx::AhxEncodeConfig::channels)
        .def_rw("encryption_type", &cricodecs::ahx::AhxEncodeConfig::encryption_type)
        .def_rw("key", &cricodecs::ahx::AhxEncodeConfig::key)
        .def_prop_rw(
            "bit_allocation_pattern",
            [](const cricodecs::ahx::AhxEncodeConfig& self) {
                return pattern_to_list(self.bit_allocation_pattern);
            },
            [](cricodecs::ahx::AhxEncodeConfig& self, const nb::object& value) {
                self.bit_allocation_pattern = pattern_from_object(value, "AHX bit_allocation_pattern");
            }
        );

    install_attr_repr(module, "AhxKey", {"start", "mult", "add"});
    install_attr_repr(module, "KeyCandidate", {"key", "score", "source_count", "evidence_count", "candidate_counts", "canonical_type9_code"});
    install_attr_repr(module, "KeyRecoveryResult", {"candidates", "source_count", "evidence_count"});
    install_attr_repr(module, "AhxDecodeConfig", {"encoding_mode", "sample_rate", "sample_count", "channels", "encryption_type", "start_offset", "key"});
    install_attr_repr(module, "AhxEncodeConfig", {"encoding_mode", "sample_rate", "channels", "encryption_type", "key", "bit_allocation_pattern"});

    module.def("default_bit_allocation_pattern", []() {
        return pattern_to_list(cricodecs::ahx::default_bit_allocation_pattern());
    });
    module.def(
        "preset_bit_allocation_pattern",
        [](cricodecs::ahx::AhxBitAllocationPreset preset) {
            return pattern_to_list(cricodecs::ahx::preset_bit_allocation_pattern(preset));
        },
        nb::arg("preset")
    );
    module.def(
        "clamp_bit_allocation_pattern",
        [](const nb::object& value) {
            return pattern_to_list(cricodecs::ahx::clamp_bit_allocation_pattern(
                pattern_from_object(value, "AHX bit_allocation_pattern")
            ));
        },
        nb::arg("bit_allocation_pattern")
    );

    module.def(
        "_key_from_type8",
        [](const std::string& key) {
            return ahx_key_from_adx_state(cricodecs::adx::key8_derive(key));
        },
        nb::arg("key")
    );
    module.def(
        "_key_from_type8_bytes",
        [](const nb::bytes& key) {
            return ahx_key_from_adx_state(cricodecs::adx::key8_derive(borrow_python_bytes(key)));
        },
        nb::arg("key")
    );
    module.def(
        "_key_from_type9",
        [](uint64_t keycode, uint16_t subkey) {
            return ahx_key_from_adx_state(cricodecs::adx::key9_derive(keycode, subkey));
        },
        nb::arg("keycode"),
        nb::arg("subkey") = 0
    );

    module.def(
        "decode",
        [](const nb::object& source, const nb::object& config_object, const nb::object& key, uint16_t subkey) {
            auto adx = load_adx_for_ahx(source);
            auto config = config_object.is_none()
                ? ahx_decode_config_from_adx(adx)
                : nb::cast<cricodecs::ahx::AhxDecodeConfig>(config_object);
            config.key = ahx_key_from_object(key, subkey);
            const auto encoded = unwrap_expected(adx.rebuild());
            auto decoded = unwrap_expected(cricodecs::ahx::decode(encoded, config));
            return pcm16_to_wav_python_bytes(
                decoded,
                config.sample_rate,
                config.channels
            );
        },
        nb::arg("source"),
        nb::arg("config") = nb::none(),
        nb::arg("key") = nb::none(),
        nb::arg("subkey") = static_cast<uint16_t>(0)
    );

    module.def(
        "recover_key",
        &recover_ahx_python,
        nb::arg("source"),
        nb::arg("same_base_key") = true,
        "Recover up to ten ranked AHX key-triplet candidates from one or more sources."
    );

    module.def(
        "decode",
        [](const nb::bytes& data, const cricodecs::ahx::AhxDecodeConfig& config) {
            const auto view = borrow_python_bytes(data);
            auto decoded = unwrap_expected(cricodecs::ahx::decode(as_byte_span(view), config));
            return pcm16_to_wav_python_bytes(
                decoded,
                config.sample_rate,
                config.channels
            );
        },
        nb::arg("data"),
        nb::arg("config")
    );

    module.def(
        "encode",
        [](const cricodecs::wav::WavContainer& wav, const cricodecs::ahx::AhxEncodeConfig& config) {
            auto effective_config = config;
            effective_config.sample_rate = wav.sample_rate();
            effective_config.channels = static_cast<uint8_t>(wav.channels());
            auto encoded = unwrap_expected(cricodecs::ahx::encode(unwrap_expected(wav.get_pcm16()), effective_config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config")
    );

    module.def(
        "encode",
        [](const nb::object& wav_source, const cricodecs::ahx::AhxEncodeConfig& config) {
            auto wav = load_wav_for_ahx(wav_source);
            auto effective_config = config;
            effective_config.sample_rate = wav.sample_rate();
            effective_config.channels = static_cast<uint8_t>(wav.channels());
            auto encoded = unwrap_expected(cricodecs::ahx::encode(unwrap_expected(wav.get_pcm16()), effective_config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config")
    );

    module.def(
        "encode",
        [](const nb::bytes& wav, const cricodecs::ahx::AhxEncodeConfig& config) {
            auto source = wav_pcm16_from_python_bytes(wav, "AHX");
            auto effective_config = config;
            effective_config.sample_rate = source.sample_rate;
            effective_config.channels = static_cast<uint8_t>(source.channels);
            auto encoded = unwrap_expected(cricodecs::ahx::encode(source.samples, effective_config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config")
    );
}

} // namespace cricodecs::python
