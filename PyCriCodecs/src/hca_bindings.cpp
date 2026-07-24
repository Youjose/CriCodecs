#include "binding_helpers.hpp"

#include "../../CriCodecs/src/hca/hca_codec.hpp"

#include <nanobind/stl/vector.h>

namespace cricodecs::python {
namespace {

[[nodiscard]] cricodecs::hca::HcaEncodeConfig default_config_for_wav(
    const cricodecs::wav::WavContainer& wav
) {
    cricodecs::hca::HcaEncodeConfig config;
    config.sample_rate = wav.sample_rate();
    config.channel_count = static_cast<uint8_t>(wav.channels());

    const auto& loops = wav.sampler().loops;
    if (!loops.empty() && loops.front().start < loops.front().end) {
        config.loop_enabled = true;
        config.loop_start = loops.front().start;
        config.loop_end = loops.front().end;
    }

    return config;
}

[[nodiscard]] cricodecs::hca::Hca load_hca_any(const nb::object& source) {
    return load_path_or_borrowed_source(source, [](const std::filesystem::path& path, std::span<const uint8_t> data) {
        if (!path.empty()) {
            return unwrap_expected(cricodecs::hca::Hca::load(path));
        }
        return unwrap_expected(cricodecs::hca::Hca::load(data));
    });
}

[[nodiscard]] cricodecs::wav::WavContainer load_wav_for_hca(const nb::object& source) {
    cricodecs::wav::WavContainer wav;
    if (auto path = python_text_path(source)) {
        unwrap_expected(wav.load(std::filesystem::path(*path)));
        return wav;
    }
    auto borrowed = borrow_python_source(source);
    unwrap_expected(wav.load(borrowed.as_span()));
    return wav;
}

[[nodiscard]] cricodecs::hca::KeyRecoveryResult recover_hca_python(
    const nb::object& source,
    const nb::object& subkeys,
    bool same_base_key) {
    std::vector<cricodecs::hca::Hca> hcas;
    if ((PyList_Check(source.ptr()) || PyTuple_Check(source.ptr()))) {
        const nb::sequence sequence = nb::borrow<nb::sequence>(source);
        hcas.reserve(nb::len(sequence));
        for (const auto item : sequence) {
            hcas.push_back(load_hca_any(nb::borrow<nb::object>(item)));
        }
    } else {
        hcas.push_back(load_hca_any(source));
    }

    std::vector<uint16_t> source_subkeys(hcas.size(), 0);
    if (!subkeys.is_none()) {
        if (PyLong_Check(subkeys.ptr())) {
            std::ranges::fill(source_subkeys, nb::cast<uint16_t>(subkeys));
        } else if (PyList_Check(subkeys.ptr()) || PyTuple_Check(subkeys.ptr())) {
            const nb::sequence sequence = nb::borrow<nb::sequence>(subkeys);
            if (nb::len(sequence) != hcas.size()) {
                raise_value_error("HCA recover_key subkeys must match the number of sources");
            }
            for (size_t index = 0; index < hcas.size(); ++index) {
                source_subkeys[index] = nb::cast<uint16_t>(sequence[index]);
            }
        } else {
            raise_type_error("HCA recover_key subkeys must be an integer, sequence, or None");
        }
    }

    std::vector<cricodecs::hca::RecoverySource> sources;
    sources.reserve(hcas.size());
    for (size_t index = 0; index < hcas.size(); ++index) {
        sources.push_back(cricodecs::hca::RecoverySource{
            .hca = &hcas[index],
            .subkey = source_subkeys[index],
            .group = index,
        });
    }
    return unwrap_expected(cricodecs::hca::recover_key(
        sources,
        same_base_key
            ? cricodecs::KeyRecoveryMode::SharedBaseKey
            : cricodecs::KeyRecoveryMode::Independent));
}

} // namespace

void bind_hca_module(nb::module_& module) {
    nb::enum_<cricodecs::KeyRecoveryMode>(module, "KeyRecoveryMode")
        .value("INDEPENDENT", cricodecs::KeyRecoveryMode::Independent)
        .value("SHARED_BASE_KEY", cricodecs::KeyRecoveryMode::SharedBaseKey);

    nb::class_<cricodecs::hca::KeyCandidate>(module, "KeyCandidate")
        .def_ro("key", &cricodecs::hca::KeyCandidate::key)
        .def_ro("score", &cricodecs::hca::KeyCandidate::score)
        .def_ro("source_count", &cricodecs::hca::KeyCandidate::source_count)
        .def_ro("evidence_count", &cricodecs::hca::KeyCandidate::evidence_count)
        .def_ro("unknown_high_bits", &cricodecs::hca::KeyCandidate::unknown_high_bits)
        .def_ro("equivalent_count", &cricodecs::hca::KeyCandidate::equivalent_count);

    nb::class_<cricodecs::hca::KeyRecoveryResult>(module, "KeyRecoveryResult")
        .def_ro("candidates", &cricodecs::hca::KeyRecoveryResult::candidates)
        .def_ro("source_count", &cricodecs::hca::KeyRecoveryResult::source_count)
        .def_ro("evidence_count", &cricodecs::hca::KeyRecoveryResult::evidence_count);
    nb::enum_<cricodecs::hca::HcaQuality>(module, "HcaQuality")
        .value("HIGHEST", cricodecs::hca::HcaQuality::Highest)
        .value("HIGH", cricodecs::hca::HcaQuality::High)
        .value("MIDDLE", cricodecs::hca::HcaQuality::Middle)
        .value("LOW", cricodecs::hca::HcaQuality::Low)
        .value("LOWEST", cricodecs::hca::HcaQuality::Lowest);

    nb::enum_<cricodecs::hca::HcaCodecChunkType>(module, "HcaCodecChunkType")
        .value("UNKNOWN", cricodecs::hca::HcaCodecChunkType::Unknown)
        .value("COMP", cricodecs::hca::HcaCodecChunkType::Comp)
        .value("DEC", cricodecs::hca::HcaCodecChunkType::Dec);

    nb::class_<cricodecs::hca::HcaFileChunk>(module, "HcaFileChunk")
        .def_ro("version", &cricodecs::hca::HcaFileChunk::version)
        .def_ro("header_size", &cricodecs::hca::HcaFileChunk::header_size);

    nb::class_<cricodecs::hca::HcaFmtChunk>(module, "HcaFmtChunk")
        .def_prop_ro("channel_count", [](const cricodecs::hca::HcaFmtChunk& chunk) { return chunk.channel_count; })
        .def_prop_ro("sample_rate", [](const cricodecs::hca::HcaFmtChunk& chunk) { return chunk.sample_rate; })
        .def_ro("frame_count", &cricodecs::hca::HcaFmtChunk::frame_count)
        .def_ro("encoder_delay", &cricodecs::hca::HcaFmtChunk::encoder_delay)
        .def_ro("encoder_padding", &cricodecs::hca::HcaFmtChunk::encoder_padding);

    nb::class_<cricodecs::hca::HcaCodecChunk>(module, "HcaCodecChunk")
        .def_ro("frame_size", &cricodecs::hca::HcaCodecChunk::frame_size)
        .def_prop_ro("type", [](const cricodecs::hca::HcaCodecChunk& chunk) { return chunk.type(); })
        .def_ro("min_resolution", &cricodecs::hca::HcaCodecChunk::min_resolution)
        .def_ro("max_resolution", &cricodecs::hca::HcaCodecChunk::max_resolution)
        .def_prop_ro("track_count", [](const cricodecs::hca::HcaCodecChunk& chunk) { return chunk.track_count; })
        .def_prop_ro("channel_config", [](const cricodecs::hca::HcaCodecChunk& chunk) { return chunk.channel_config; })
        .def_ro("total_band_count", &cricodecs::hca::HcaCodecChunk::total_band_count)
        .def_ro("base_band_count", &cricodecs::hca::HcaCodecChunk::base_band_count)
        .def_ro("stereo_band_count", &cricodecs::hca::HcaCodecChunk::stereo_band_count)
        .def_ro("bands_per_hfr_group", &cricodecs::hca::HcaCodecChunk::bands_per_hfr_group)
        .def_ro("hfr_group_count", &cricodecs::hca::HcaCodecChunk::hfr_group_count)
        .def_prop_ro("ms_stereo", [](const cricodecs::hca::HcaCodecChunk& chunk) { return chunk.ms_stereo(); })
        .def_prop_ro("uses_ms_stereo", &cricodecs::hca::HcaCodecChunk::uses_ms_stereo);

    nb::class_<cricodecs::hca::HcaVbrChunk>(module, "HcaVbrChunk")
        .def_ro("max_frame_size", &cricodecs::hca::HcaVbrChunk::max_frame_size)
        .def_ro("noise_level", &cricodecs::hca::HcaVbrChunk::noise_level)
        .def_prop_ro("enabled", &cricodecs::hca::HcaVbrChunk::enabled);

    nb::class_<cricodecs::hca::HcaAthChunk>(module, "HcaAthChunk")
        .def_ro("type", &cricodecs::hca::HcaAthChunk::type)
        .def_prop_ro("uses_curve", &cricodecs::hca::HcaAthChunk::uses_curve);

    nb::class_<cricodecs::hca::HcaLoopChunk>(module, "HcaLoopChunk")
        .def_ro("start_frame", &cricodecs::hca::HcaLoopChunk::start_frame)
        .def_ro("end_frame", &cricodecs::hca::HcaLoopChunk::end_frame)
        .def_ro("start_delay", &cricodecs::hca::HcaLoopChunk::start_delay)
        .def_ro("end_padding", &cricodecs::hca::HcaLoopChunk::end_padding)
        .def_prop_ro("enabled", &cricodecs::hca::HcaLoopChunk::enabled);

    nb::class_<cricodecs::hca::HcaCipherChunk>(module, "HcaCipherChunk")
        .def_ro("type", &cricodecs::hca::HcaCipherChunk::type)
        .def_prop_ro("encrypted", &cricodecs::hca::HcaCipherChunk::encrypted);

    nb::class_<cricodecs::hca::HcaRvaChunk>(module, "HcaRvaChunk")
        .def_ro("volume", &cricodecs::hca::HcaRvaChunk::volume)
        .def_prop_ro("has_volume_scale", &cricodecs::hca::HcaRvaChunk::has_volume_scale);

    nb::class_<cricodecs::hca::HcaCommentChunk>(module, "HcaCommentChunk")
        .def_ro("length", &cricodecs::hca::HcaCommentChunk::length)
        .def_prop_ro("has_text", &cricodecs::hca::HcaCommentChunk::has_text);

    nb::class_<cricodecs::hca::HcaHeader>(module, "HcaHeader")
        .def_ro("file", &cricodecs::hca::HcaHeader::file)
        .def_ro("fmt", &cricodecs::hca::HcaHeader::fmt)
        .def_ro("codec", &cricodecs::hca::HcaHeader::codec)
        .def_ro("vbr", &cricodecs::hca::HcaHeader::vbr)
        .def_ro("loop", &cricodecs::hca::HcaHeader::loop)
        .def_ro("rva", &cricodecs::hca::HcaHeader::rva)
        .def_ro("ath", &cricodecs::hca::HcaHeader::ath)
        .def_ro("cipher", &cricodecs::hca::HcaHeader::cipher)
        .def_ro("comment", &cricodecs::hca::HcaHeader::comment)
        .def("sample_count", &cricodecs::hca::HcaHeader::sample_count);

    nb::class_<cricodecs::hca::HcaEncodeConfig>(module, "HcaEncodeConfig")
        .def(nb::init<>())
        .def_rw("sample_rate", &cricodecs::hca::HcaEncodeConfig::sample_rate)
        .def_rw("channel_count", &cricodecs::hca::HcaEncodeConfig::channel_count)
        .def_rw("version", &cricodecs::hca::HcaEncodeConfig::version)
        .def_rw("bitrate", &cricodecs::hca::HcaEncodeConfig::bitrate)
        .def_rw("quality", &cricodecs::hca::HcaEncodeConfig::quality)
        .def_rw("loop_enabled", &cricodecs::hca::HcaEncodeConfig::loop_enabled)
        .def_rw("loop_start", &cricodecs::hca::HcaEncodeConfig::loop_start)
        .def_rw("loop_end", &cricodecs::hca::HcaEncodeConfig::loop_end)
        .def_rw("ms_stereo", &cricodecs::hca::HcaEncodeConfig::ms_stereo)
        .def_rw("keycode", &cricodecs::hca::HcaEncodeConfig::keycode)
        .def_rw("subkey", &cricodecs::hca::HcaEncodeConfig::subkey);

    nb::class_<cricodecs::hca::Hca>(module, "Hca")
        .def_static("load", &load_hca_any, nb::arg("source"))
        .def_static(
            "load_bytes",
            [](const nb::bytes& data) {
                const auto view = borrow_python_bytes(data);
                return unwrap_expected(cricodecs::hca::Hca::load(as_byte_span(view)));
            },
            nb::arg("data"),
            "Load an HCA object from raw bytes."
        )
        .def_prop_ro(
            "source_path",
            [](const cricodecs::hca::Hca& hca) { return path_or_none(hca.source_path()); }
        )
        .def(
            "header",
            [](const cricodecs::hca::Hca& hca) -> const cricodecs::hca::HcaHeader& {
                return hca.header();
            },
            nb::rv_policy::reference_internal,
            "Return the parsed HCA header metadata."
        )
        .def(
            "decode",
            [](const cricodecs::hca::Hca& hca, uint64_t keycode, uint16_t subkey) {
                auto pcm = unwrap_expected(hca.decode(keycode, subkey));
                const auto& header = hca.header();
                return pcm16_to_wav_python_bytes(
                    pcm,
                    header.fmt.sample_rate,
                    static_cast<uint16_t>(header.fmt.channel_count)
                );
            },
            nb::arg("keycode") = 0,
            nb::arg("subkey") = 0,
            "Decode the loaded HCA bytes into RIFF WAV bytes."
        )
        .def(
            "encrypt",
            [](const cricodecs::hca::Hca& hca, uint16_t cipher_type, uint64_t keycode, uint16_t subkey) {
                auto output = unwrap_expected(hca.encrypt(cipher_type, keycode, subkey));
                return to_python_bytes(output);
            },
            nb::arg("cipher_type"),
            nb::arg("keycode") = 0,
            nb::arg("subkey") = 0,
            "Encrypt the loaded HCA bytes and return the transformed stream."
        )
        .def(
            "decrypt",
            [](const cricodecs::hca::Hca& hca, uint64_t keycode, uint16_t subkey) {
                auto output = unwrap_expected(hca.decrypt(keycode, subkey));
                return to_python_bytes(output);
            },
            nb::arg("keycode") = 0,
            nb::arg("subkey") = 0,
            "Decrypt the loaded HCA bytes and return the transformed stream."
        )
        .def(
            "rebuild",
            [](const cricodecs::hca::Hca& hca) {
                return to_python_bytes(unwrap_expected(hca.rebuild()));
            },
            "Return the current HCA byte stream."
        )
        .def(
            "recover_key",
            [](const cricodecs::hca::Hca& hca, uint16_t subkey) {
                const cricodecs::hca::RecoverySource source{
                    .hca = &hca,
                    .subkey = subkey,
                    .group = 0,
                };
                return unwrap_expected(cricodecs::hca::recover_key(
                    std::span<const cricodecs::hca::RecoverySource>(&source, 1)));
            },
            nb::arg("subkey") = 0,
            "Recover up to ten ranked canonical low-56 base-key candidates; "
            "the original 64-bit key's upper byte is not observable."
        );

    install_attr_repr(module, "HcaFileChunk", {"version", "header_size"});
    install_attr_repr(module, "HcaFmtChunk", {"channel_count", "sample_rate", "frame_count", "encoder_delay", "encoder_padding"});
    install_attr_repr(module, "HcaCodecChunk", {"frame_size", "type", "min_resolution", "max_resolution", "track_count", "channel_config", "total_band_count", "base_band_count", "stereo_band_count", "bands_per_hfr_group", "hfr_group_count", "ms_stereo"});
    install_attr_repr(module, "HcaVbrChunk", {"max_frame_size", "noise_level"});
    install_attr_repr(module, "HcaAthChunk", {"type"});
    install_attr_repr(module, "HcaLoopChunk", {"start_frame", "end_frame", "start_delay", "end_padding"});
    install_attr_repr(module, "HcaCipherChunk", {"type"});
    install_attr_repr(module, "HcaRvaChunk", {"volume"});
    install_attr_repr(module, "HcaCommentChunk", {"length"});
    install_attr_repr(module, "HcaHeader", {"file", "fmt", "codec", "vbr", "loop", "rva", "ath", "cipher", "comment"});
    install_attr_repr(module, "HcaEncodeConfig", {"sample_rate", "channel_count", "version", "bitrate", "quality", "loop_enabled", "loop_start", "loop_end", "ms_stereo", "keycode", "subkey"});
    install_attr_repr(module, "KeyCandidate", {"key", "score", "source_count", "evidence_count", "unknown_high_bits", "equivalent_count"});
    install_attr_repr(module, "KeyRecoveryResult", {"candidates", "source_count", "evidence_count"});
    install_attr_repr(module, "Hca", {"source_path", "header"});

    module.attr("NativeHca") = module.attr("Hca");

    module.def(
        "decode",
        [](const nb::object& source, uint64_t keycode, uint16_t subkey) {
            auto hca = load_hca_any(source);
            auto pcm = unwrap_expected(hca.decode(keycode, subkey));
            const auto& header = hca.header();
            return pcm16_to_wav_python_bytes(
                pcm,
                header.fmt.sample_rate,
                static_cast<uint16_t>(header.fmt.channel_count)
            );
        },
        nb::arg("source"),
        nb::arg("keycode") = 0,
        nb::arg("subkey") = 0,
        "Decode HCA bytes into RIFF WAV bytes."
    );

    module.def(
        "encode",
        [](const cricodecs::wav::WavContainer& wav) {
            auto encoded = unwrap_expected(cricodecs::hca::encode(wav, default_config_for_wav(wav)));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        "Encode a loaded WAV object into an HCA stream using a derived default config."
    );

    module.def(
        "encode",
        [](const cricodecs::wav::WavContainer& wav, const cricodecs::hca::HcaEncodeConfig& config) {
            auto encoded = unwrap_expected(cricodecs::hca::encode(wav, config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config"),
        "Encode a loaded WAV object into an HCA stream."
    );

    module.def(
        "encode",
        [](const nb::object& wav_source, const cricodecs::hca::HcaEncodeConfig& config) {
            auto wav = load_wav_for_hca(wav_source);
            auto effective_config = config;
            if (effective_config.sample_rate == 0) {
                effective_config.sample_rate = wav.sample_rate();
            }
            if (effective_config.channel_count == 0) {
                effective_config.channel_count = static_cast<uint8_t>(wav.channels());
            }
            auto encoded = unwrap_expected(cricodecs::hca::encode(wav, effective_config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config")
    );

    module.def(
        "encode",
        [](const nb::bytes& wav, const cricodecs::hca::HcaEncodeConfig& config) {
            auto source = wav_pcm16_from_python_bytes(wav, "HCA");
            auto effective_config = config;
            effective_config.sample_rate = source.sample_rate;
            effective_config.channel_count = static_cast<uint8_t>(source.channels);
            auto encoded = unwrap_expected(cricodecs::hca::encode(source.samples, effective_config));
            return to_python_bytes(encoded);
        },
        nb::arg("wav"),
        nb::arg("config"),
        "Encode RIFF WAV bytes into an HCA stream."
    );

    module.def(
        "encrypt",
        [](const nb::object& source, uint16_t cipher_type, uint64_t keycode, uint16_t subkey) {
            auto hca = load_hca_any(source);
            auto output = unwrap_expected(hca.encrypt(cipher_type, keycode, subkey));
            return to_python_bytes(output);
        },
        nb::arg("source"),
        nb::arg("cipher_type"),
        nb::arg("keycode") = 0,
        nb::arg("subkey") = 0,
        "Encrypt an HCA stream and return the transformed bytes."
    );

    module.def(
        "decrypt",
        [](const nb::object& source, uint64_t keycode, uint16_t subkey) {
            auto hca = load_hca_any(source);
            auto output = unwrap_expected(hca.decrypt(keycode, subkey));
            return to_python_bytes(output);
        },
        nb::arg("source"),
        nb::arg("keycode") = 0,
        nb::arg("subkey") = 0,
        "Decrypt an HCA stream and return the transformed bytes."
    );

    module.def("load", &load_hca_any, nb::arg("source"));
    module.def(
        "recover_key",
        &recover_hca_python,
        nb::arg("source"),
        nb::arg("subkeys") = nb::none(),
        nb::arg("same_base_key") = true,
        "Recover up to ten ranked canonical low-56 HCA base-key candidates; "
        "the original 64-bit key's upper byte is not observable."
    );
}

} // namespace cricodecs::python
