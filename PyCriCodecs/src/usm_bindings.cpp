#include "binding_helpers.hpp"

#include <filesystem>
#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/usm/usm_container.hpp"
#include "../../CriCodecs/src/usm/usm_key_recovery.hpp"
#include "../../CriCodecs/src/utilities/text_encoding.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] cricodecs::text::EncodingOptions encoding_options_from_python(const nb::handle& encoding) {
    if (encoding.is_none()) {
        return {};
    }

    std::string name = nb::cast<std::string>(encoding);
    cricodecs::text::EncodingOptions options{std::move(name)};
    if (options.encoding->empty() || cricodecs::text::is_auto_encoding(options)) {
        return {};
    }
    return options;
}

[[nodiscard]] uint64_t usm_key_from_python(const nb::handle& key) {
    if (key.is_none()) {
        return 0;
    }
    return nb::cast<uint64_t>(key);
}

void apply_usm_key(cricodecs::usm::UsmReader& reader, const nb::handle& key) {
    reader.set_key(usm_key_from_python(key));
}

[[nodiscard]] cricodecs::usm::UsmReader load_usm_any(
    const nb::object& source,
    const nb::object& encoding,
    const nb::object& key
) {
    cricodecs::usm::UsmReader reader;
    reader.set_encoding(encoding_options_from_python(encoding));
    apply_usm_key(reader, key);
    if (auto path = python_text_path(source)) {
        unwrap_expected(reader.load(std::filesystem::path(*path)));
        return reader;
    }
    auto borrowed = borrow_python_source(source);
    unwrap_expected(reader.load(borrowed.as_span()));
    return reader;
}

[[nodiscard]] cricodecs::usm::KeyRecoveryResult recover_usm_python(
    const nb::object& source,
    bool same_base_key,
    const nb::object& encoding)
{
    std::vector<cricodecs::usm::UsmReader> movies;
    if (PyList_Check(source.ptr()) || PyTuple_Check(source.ptr())) {
        const auto sequence = nb::borrow<nb::sequence>(source);
        movies.reserve(nb::len(sequence));
        for (const auto item : sequence) {
            movies.push_back(load_usm_any(nb::borrow<nb::object>(item), encoding, nb::none()));
        }
    } else {
        movies.push_back(load_usm_any(source, encoding, nb::none()));
    }
    if (movies.empty()) raise_value_error("USM key recovery requires at least one source");

    struct Aggregate {
        cricodecs::usm::KeyCandidate candidate;
        size_t occurrence_count = 0;
        double score_sum = 0.0;
        double audio_score_sum = 0.0;
        double hca_score_sum = 0.0;
        size_t recommended_count = 0;
    };
    std::map<uint64_t, Aggregate> aggregated;
    size_t evidence_count = 0;
    for (const auto& movie : movies) {
        auto recovered = unwrap_expected(cricodecs::usm::recover_key(movie));
        evidence_count += recovered.evidence_count;
        for (size_t candidate_index = 0; candidate_index < recovered.candidates.size(); ++candidate_index) {
            const auto& candidate = recovered.candidates[candidate_index];
            auto& aggregate = aggregated[candidate.key];
            if (aggregate.occurrence_count == 0u) aggregate.candidate = candidate;
            ++aggregate.occurrence_count;
            aggregate.score_sum += candidate.score;
            aggregate.audio_score_sum += candidate.audio_score;
            aggregate.hca_score_sum += candidate.hca_score;
            if (candidate_index == 0u) ++aggregate.recommended_count;
            if (aggregate.occurrence_count > 1u) {
                aggregate.candidate.source_count += candidate.source_count;
                aggregate.candidate.evidence_count += candidate.evidence_count;
                aggregate.candidate.sample_blocks += candidate.sample_blocks;
                aggregate.candidate.video_chunks += candidate.video_chunks;
                aggregate.candidate.audio_chunks += candidate.audio_chunks;
                aggregate.candidate.hca_streams += candidate.hca_streams;
                aggregate.candidate.hca_video_supported =
                    aggregate.candidate.hca_video_supported || candidate.hca_video_supported;
            }
        }
    }

    std::vector<cricodecs::usm::KeyCandidate> candidates;
    for (auto& [_, aggregate] : aggregated) {
        if (same_base_key && aggregate.occurrence_count != movies.size()) continue;
        aggregate.candidate.score = static_cast<float>(aggregate.score_sum / aggregate.occurrence_count);
        aggregate.candidate.audio_score = static_cast<float>(
            aggregate.audio_score_sum / aggregate.occurrence_count);
        aggregate.candidate.hca_score = static_cast<float>(
            aggregate.hca_score_sum / aggregate.occurrence_count);
        candidates.push_back(aggregate.candidate);
    }
    if (candidates.empty()) {
        raise_value_error("USM key recovery found no candidate shared by every supplied movie");
    }
    std::ranges::sort(candidates, [&](const auto& left, const auto& right) {
        const auto left_recommended = aggregated.at(left.key).recommended_count;
        const auto right_recommended = aggregated.at(right.key).recommended_count;
        if (left_recommended != right_recommended) return left_recommended > right_recommended;
        if (left.source_count != right.source_count) return left.source_count > right.source_count;
        if (left.score != right.score) return left.score > right.score;
        return left.key < right.key;
    });
    if (candidates.size() > cricodecs::MaxKeyRecoveryCandidates) {
        candidates.resize(cricodecs::MaxKeyRecoveryCandidates);
    }
    return {
        .candidates = std::move(candidates),
        .source_count = movies.size(),
        .evidence_count = evidence_count,
    };
}

} // namespace

void bind_usm_module(nb::module_& module) {
    nb::class_<cricodecs::usm::KeyCandidate>(module, "KeyCandidate")
        .def_ro("key", &cricodecs::usm::KeyCandidate::key)
        .def_ro("score", &cricodecs::usm::KeyCandidate::score)
        .def_ro("source_count", &cricodecs::usm::KeyCandidate::source_count)
        .def_ro("evidence_count", &cricodecs::usm::KeyCandidate::evidence_count)
        .def_ro("sample_blocks", &cricodecs::usm::KeyCandidate::sample_blocks)
        .def_ro("video_chunks", &cricodecs::usm::KeyCandidate::video_chunks)
        .def_ro("audio_chunks", &cricodecs::usm::KeyCandidate::audio_chunks)
        .def_ro("audio_score", &cricodecs::usm::KeyCandidate::audio_score)
        .def_ro("hca_streams", &cricodecs::usm::KeyCandidate::hca_streams)
        .def_ro("hca_score", &cricodecs::usm::KeyCandidate::hca_score)
        .def_ro("hca_video_supported", &cricodecs::usm::KeyCandidate::hca_video_supported);

    nb::class_<cricodecs::usm::KeyRecoveryResult>(module, "KeyRecoveryResult")
        .def_ro("candidates", &cricodecs::usm::KeyRecoveryResult::candidates)
        .def_ro("source_count", &cricodecs::usm::KeyRecoveryResult::source_count)
        .def_ro("evidence_count", &cricodecs::usm::KeyRecoveryResult::evidence_count);

    nb::enum_<cricodecs::usm::UsmChunkType>(module, "UsmChunkType")
        .value("CRID", cricodecs::usm::UsmChunkType::CRID)
        .value("SFSH", cricodecs::usm::UsmChunkType::SFSH)
        .value("AHX", cricodecs::usm::UsmChunkType::AHX)
        .value("ELM", cricodecs::usm::UsmChunkType::ELM)
        .value("ATP", cricodecs::usm::UsmChunkType::ATP)
        .value("PST", cricodecs::usm::UsmChunkType::PST)
        .value("SFV", cricodecs::usm::UsmChunkType::SFV)
        .value("SFA", cricodecs::usm::UsmChunkType::SFA)
        .value("ALP", cricodecs::usm::UsmChunkType::ALP)
        .value("CUE", cricodecs::usm::UsmChunkType::CUE)
        .value("SBT", cricodecs::usm::UsmChunkType::SBT)
        .value("STA", cricodecs::usm::UsmChunkType::STA)
        .value("USR", cricodecs::usm::UsmChunkType::USR);

    nb::enum_<cricodecs::usm::UsmSubtitleFormat>(module, "UsmSubtitleFormat")
        .value("AUTO", cricodecs::usm::UsmSubtitleFormat::Auto)
        .value("SOURCE_TEXT", cricodecs::usm::UsmSubtitleFormat::SourceText)
        .value("SRT", cricodecs::usm::UsmSubtitleFormat::Srt)
        .value("ASS", cricodecs::usm::UsmSubtitleFormat::Ass)
        .value("SBT", cricodecs::usm::UsmSubtitleFormat::Sbt);

    nb::enum_<cricodecs::usm::UsmAudioCodec>(module, "UsmAudioCodec")
        .value("ADX", cricodecs::usm::UsmAudioCodec::Adx)
        .value("HCA", cricodecs::usm::UsmAudioCodec::Hca)
        .value("UNKNOWN", cricodecs::usm::UsmAudioCodec::Unknown);

    nb::class_<cricodecs::usm::UsmStreamInfo>(module, "UsmStreamInfo")
        .def_ro("filename", &cricodecs::usm::UsmStreamInfo::filename)
        .def_prop_ro("filename_raw", [](const cricodecs::usm::UsmStreamInfo& stream) {
            return to_python_bytes(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(stream.filename_raw.data()),
                stream.filename_raw.size()
            ));
        })
        .def_ro("stream_id", &cricodecs::usm::UsmStreamInfo::stream_id)
        .def_ro("channel_no", &cricodecs::usm::UsmStreamInfo::channel_no)
        .def_ro("audio_codec", &cricodecs::usm::UsmStreamInfo::audio_codec)
        .def_ro("fmtver", &cricodecs::usm::UsmStreamInfo::fmtver)
        .def_ro("filesize", &cricodecs::usm::UsmStreamInfo::filesize)
        .def_ro("minchk", &cricodecs::usm::UsmStreamInfo::minchk)
        .def_ro("minbuf", &cricodecs::usm::UsmStreamInfo::minbuf)
        .def_ro("avbps", &cricodecs::usm::UsmStreamInfo::avbps);

    nb::class_<cricodecs::usm::UsmBuildInput::AudioTrack>(module, "UsmMuxAudioTrack")
        .def(nb::init<>())
        .def("__init__", [](
            cricodecs::usm::UsmBuildInput::AudioTrack* self,
            const nb::object& path,
            std::optional<bool> encrypt,
            std::optional<uint8_t> channel_no
        ) {
            cricodecs::usm::UsmBuildInput::AudioTrack track;
            track.path = require_python_path(path, "path");
            track.encrypt = std::move(encrypt);
            track.channel_no = channel_no;
            new (self) cricodecs::usm::UsmBuildInput::AudioTrack(std::move(track));
        }, nb::arg("path"), nb::arg("encrypt") = nb::none(), nb::arg("channel_no") = nb::none())
        .def_prop_rw("path", [](const cricodecs::usm::UsmBuildInput::AudioTrack& self) {
            return self.path.generic_string();
        }, [](cricodecs::usm::UsmBuildInput::AudioTrack& self, const nb::object& path) {
            self.path = require_python_path(path, "path");
        })
        .def_rw("encrypt", &cricodecs::usm::UsmBuildInput::AudioTrack::encrypt)
        .def_rw("channel_no", &cricodecs::usm::UsmBuildInput::AudioTrack::channel_no);

    nb::class_<cricodecs::usm::UsmBuildInput::SubtitleTrack>(module, "UsmMuxSubtitleTrack")
        .def(nb::init<>())
        .def("__init__", [](
            cricodecs::usm::UsmBuildInput::SubtitleTrack* self,
            const nb::object& path,
            uint32_t language_id,
            cricodecs::usm::UsmSubtitleFormat format,
            std::optional<uint8_t> channel_no
        ) {
            cricodecs::usm::UsmBuildInput::SubtitleTrack track;
            track.path = require_python_path(path, "path");
            track.language_id = language_id;
            track.format = format;
            track.channel_no = channel_no;
            new (self) cricodecs::usm::UsmBuildInput::SubtitleTrack(std::move(track));
        },
            nb::arg("path"),
            nb::arg("language_id") = static_cast<uint32_t>(0),
            nb::arg("format") = cricodecs::usm::UsmSubtitleFormat::Auto,
            nb::arg("channel_no") = nb::none()
        )
        .def_prop_rw("path", [](const cricodecs::usm::UsmBuildInput::SubtitleTrack& self) {
            return self.path.generic_string();
        }, [](cricodecs::usm::UsmBuildInput::SubtitleTrack& self, const nb::object& path) {
            self.path = require_python_path(path, "path");
        })
        .def_rw("language_id", &cricodecs::usm::UsmBuildInput::SubtitleTrack::language_id)
        .def_rw("format", &cricodecs::usm::UsmBuildInput::SubtitleTrack::format)
        .def_rw("channel_no", &cricodecs::usm::UsmBuildInput::SubtitleTrack::channel_no);

    nb::class_<cricodecs::usm::UsmBuildInput>(module, "UsmMuxConfig")
        .def(nb::init<>())
        .def("__init__", [](
            cricodecs::usm::UsmBuildInput* self,
            const nb::object& video_path,
            const std::vector<cricodecs::usm::UsmBuildInput::AudioTrack>& audio_tracks,
            const std::vector<cricodecs::usm::UsmBuildInput::SubtitleTrack>& subtitle_tracks,
            std::optional<bool> encrypt_audio,
            const nb::object& key,
            const nb::object& encoding
        ) {
            cricodecs::usm::UsmBuildInput input;
            input.video_path = require_python_path(video_path, "video_path");
            input.audio_tracks = audio_tracks;
            input.subtitle_tracks = subtitle_tracks;
            input.encrypt_audio = std::move(encrypt_audio);
            input.key = usm_key_from_python(key);
            input.encoding = encoding_options_from_python(encoding);
            new (self) cricodecs::usm::UsmBuildInput(std::move(input));
        },
            nb::arg("video_path"),
            nb::arg("audio_tracks") = std::vector<cricodecs::usm::UsmBuildInput::AudioTrack>{},
            nb::arg("subtitle_tracks") = std::vector<cricodecs::usm::UsmBuildInput::SubtitleTrack>{},
            nb::arg("encrypt_audio") = nb::none(),
            nb::arg("key") = nb::none(),
            nb::arg("encoding") = nb::none()
        )
        .def_prop_rw("video_path", [](const cricodecs::usm::UsmBuildInput& self) {
            return self.video_path.generic_string();
        }, [](cricodecs::usm::UsmBuildInput& self, const nb::object& path) {
            self.video_path = require_python_path(path, "video_path");
        })
        .def_rw("audio_tracks", &cricodecs::usm::UsmBuildInput::audio_tracks)
        .def_rw("subtitle_tracks", &cricodecs::usm::UsmBuildInput::subtitle_tracks)
        .def_rw("encrypt_audio", &cricodecs::usm::UsmBuildInput::encrypt_audio)
        .def_prop_rw("key", [](const cricodecs::usm::UsmBuildInput& self) -> nb::object {
            return self.key == 0 ? nb::none() : nb::cast(self.key);
        }, [](cricodecs::usm::UsmBuildInput& self, const nb::object& key) {
            self.key = usm_key_from_python(key);
        });

    nb::class_<cricodecs::usm::UsmReader>(module, "Usm")
        .def_static(
            "load",
            [](const nb::object& source, const nb::object& encoding, const nb::object& key) {
                cricodecs::usm::UsmReader reader;
                reader.set_encoding(encoding_options_from_python(encoding));
                apply_usm_key(reader, key);
                if (auto path = python_text_path(source)) {
                    unwrap_expected(reader.load(std::filesystem::path(*path)));
                    return reader;
                }
                auto borrowed = borrow_python_source(source);
                unwrap_expected(reader.load(borrowed.as_span()));
                return reader;
            },
            nb::arg("source"),
            nb::arg("encoding") = nb::none(),
            nb::arg("key") = nb::none(),
            "Load a USM container from a filesystem path."
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data, const nb::object& encoding, const nb::object& key) {
                const auto data_view = borrow_python_bytes(data);
                cricodecs::usm::UsmReader reader;
                reader.set_encoding(encoding_options_from_python(encoding));
                apply_usm_key(reader, key);
                unwrap_expected(reader.load(as_byte_span(data_view)));
                return reader;
            },
            nb::arg("data"),
            nb::arg("encoding") = nb::none(),
            nb::arg("key") = nb::none(),
            "Load a USM container from raw bytes."
        )
        .def_prop_ro("source_path", [](const cricodecs::usm::UsmReader& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("container_filename", [](const cricodecs::usm::UsmReader& self) {
            return std::string(self.container_filename());
        })
        .def_prop_ro("stream_count", [](const cricodecs::usm::UsmReader& self) {
            return self.streams().size();
        })
        .def_prop_ro("streams", [](const cricodecs::usm::UsmReader& self) {
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            return streams;
        })
        .def("info", [](const cricodecs::usm::UsmReader& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("container_filename") = std::string(self.container_filename());
            info.attr("stream_count") = self.streams().size();
            nb::list streams;
            for (const auto& stream : self.streams()) {
                streams.append(stream);
            }
            info.attr("streams") = streams;
            return info;
        })
        .def("stream", [](const cricodecs::usm::UsmReader& self, uint32_t index) {
            const auto& streams = self.streams();
            if (index >= streams.size()) {
                raise_value_error("USM stream index is out of range");
            }
            return streams[index];
        }, nb::arg("index"))
        .def(
            "stream_bytes",
            [](cricodecs::usm::UsmReader& self, uint32_t index) {
                return to_python_bytes(unwrap_expected(self.extract_stream(index)));
            },
            nb::arg("index")
        )
        .def(
            "stream_sample",
            [](const cricodecs::usm::UsmReader& self, uint32_t index, size_t max_bytes) {
                return to_python_bytes(unwrap_expected(self.extract_stream_sample(index, max_bytes)));
            },
            nb::arg("index"),
            nb::arg("max_bytes") = 4096
        )
        .def(
            "extract_file",
            [](cricodecs::usm::UsmReader& self, uint32_t index, const nb::object& output_path) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path")));
            },
            nb::arg("index"),
            nb::arg("output_path")
        )
        .def(
            "extract",
            [](cricodecs::usm::UsmReader& self, const std::string& output_dir) {
                unwrap_expected(self.extract(std::filesystem::path(output_dir)));
            },
            nb::arg("output_dir")
        )
        .def(
            "set_key",
            [](cricodecs::usm::UsmReader& self, const nb::object& key) {
                apply_usm_key(self, key);
            },
            nb::arg("key") = nb::none()
        )
        .def(
            "encrypt",
            [](const cricodecs::usm::UsmReader& self) {
                return to_python_bytes(unwrap_expected(self.encrypt()));
            }
        )
        .def(
            "decrypt",
            [](const cricodecs::usm::UsmReader& self) {
                return to_python_bytes(unwrap_expected(self.decrypt()));
            }
        )
        .def("recover_key", [](const cricodecs::usm::UsmReader& self) {
            return unwrap_expected(cricodecs::usm::recover_key(self));
        })
        .def(
            "demux",
            [](cricodecs::usm::UsmReader& self) {
                nb::dict streams;
                for (auto&& [name, bytes] : unwrap_expected(self.demux())) {
                    streams[nb::str(name.c_str())] =
                        to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
                }
                return streams;
            }
        );

    install_attr_repr(module, "UsmStreamInfo", {"filename", "stream_id", "channel_no", "audio_codec", "fmtver", "filesize", "minchk", "minbuf", "avbps"});
    install_attr_repr(module, "KeyCandidate", {"key", "score", "source_count", "evidence_count", "sample_blocks", "video_chunks", "audio_chunks", "audio_score", "hca_streams", "hca_score", "hca_video_supported"});
    install_attr_repr(module, "KeyRecoveryResult", {"candidates", "source_count", "evidence_count"});
    install_attr_repr(module, "UsmMuxAudioTrack", {"path", "encrypt", "channel_no"});
    install_attr_repr(module, "UsmMuxSubtitleTrack", {"path", "language_id", "format", "channel_no"});
    install_attr_repr(module, "UsmMuxConfig", {"video_path", "audio_tracks", "subtitle_tracks", "encrypt_audio", "key"});
    install_attr_repr(module, "Usm", {"source_path", "container_filename", "stream_count", "streams"});

    module.def(
        "mux",
        [](const cricodecs::usm::UsmBuildInput& input) {
            cricodecs::usm::UsmBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("config")
    );
    module.def(
        "mux",
        [](const cricodecs::usm::UsmBuildInput& input, const nb::object& output_path) {
            cricodecs::usm::UsmBuilder builder;
            unwrap_expected(builder.build_to_file(require_python_path(output_path, "output_path"), input));
        },
        nb::arg("config"),
        nb::arg("output_path")
    );

    module.def(
        "mux",
        [](
            const nb::object& video_path,
            const std::vector<nb::object>& audio_paths,
            const std::vector<std::optional<bool>>& audio_encrypt,
            std::optional<bool> encrypt_audio,
            const nb::object& key,
            const nb::object& encoding
        ) {
            if (!audio_encrypt.empty() && audio_encrypt.size() != audio_paths.size()) {
                raise_value_error("USM mux audio_encrypt size must match audio_paths");
            }

            cricodecs::usm::UsmBuildInput input;
            input.video_path = require_python_path(video_path, "video_path");
            input.encoding = encoding_options_from_python(encoding);
            input.encrypt_audio = std::move(encrypt_audio);
            input.key = usm_key_from_python(key);
            for (size_t index = 0; index < audio_paths.size(); ++index) {
                input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                    .path = require_python_path(audio_paths[index], "audio_paths"),
                    .encrypt = audio_encrypt.empty() ? std::nullopt : audio_encrypt[index],
                });
            }

            cricodecs::usm::UsmBuilder builder;
            return to_python_bytes(unwrap_expected(builder.build(input)));
        },
        nb::arg("video_path"),
        nb::arg("audio_paths") = std::vector<nb::object>{},
        nb::arg("audio_encrypt") = std::vector<std::optional<bool>>{},
        nb::arg("encrypt_audio") = nb::none(),
        nb::arg("key") = nb::none(),
        nb::arg("encoding") = nb::none()
    );

    module.def(
        "mux_to_file",
        [](
            const nb::object& output_path,
            const nb::object& video_path,
            const std::vector<nb::object>& audio_paths,
            const std::vector<std::optional<bool>>& audio_encrypt,
            std::optional<bool> encrypt_audio,
            const nb::object& key,
            const nb::object& encoding
        ) {
            if (!audio_encrypt.empty() && audio_encrypt.size() != audio_paths.size()) {
                raise_value_error("USM mux audio_encrypt size must match audio_paths");
            }

            cricodecs::usm::UsmBuildInput input;
            input.video_path = require_python_path(video_path, "video_path");
            input.encoding = encoding_options_from_python(encoding);
            input.encrypt_audio = std::move(encrypt_audio);
            input.key = usm_key_from_python(key);
            for (size_t index = 0; index < audio_paths.size(); ++index) {
                input.audio_tracks.push_back(cricodecs::usm::UsmBuildInput::AudioTrack{
                    .path = require_python_path(audio_paths[index], "audio_paths"),
                    .encrypt = audio_encrypt.empty() ? std::nullopt : audio_encrypt[index],
                });
            }

            cricodecs::usm::UsmBuilder builder;
            unwrap_expected(builder.build_to_file(require_python_path(output_path, "output_path"), input));
        },
        nb::arg("output_path"),
        nb::arg("video_path"),
        nb::arg("audio_paths") = std::vector<nb::object>{},
        nb::arg("audio_encrypt") = std::vector<std::optional<bool>>{},
        nb::arg("encrypt_audio") = nb::none(),
        nb::arg("key") = nb::none(),
        nb::arg("encoding") = nb::none()
    );

    module.def(
        "sbt_to_text",
        [](const nb::bytes& data) {
            const auto view = borrow_python_bytes(data);
            return unwrap_expected(cricodecs::usm::sbt_to_subtitle_source_text(as_byte_span(view)));
        },
        nb::arg("data")
    );
    module.def(
        "text_to_sbt",
        [](const std::string& text, uint32_t language_id) {
            return to_python_bytes(unwrap_expected(cricodecs::usm::subtitle_source_text_to_sbt(text, language_id)));
        },
        nb::arg("text"),
        nb::arg("language_id") = static_cast<uint32_t>(0)
    );
    module.def(
        "sbt_to_srt",
        [](const nb::bytes& data) {
            const auto view = borrow_python_bytes(data);
            return unwrap_expected(cricodecs::usm::sbt_to_srt(as_byte_span(view)));
        },
        nb::arg("data")
    );
    module.def(
        "sbt_to_srt_tracks",
        [](const nb::bytes& data) {
            const auto view = borrow_python_bytes(data);
            nb::dict tracks;
            for (const auto& [language_id, srt] : unwrap_expected(cricodecs::usm::sbt_to_srt_tracks(as_byte_span(view)))) {
                tracks[nb::int_(language_id)] = nb::str(srt.c_str());
            }
            return tracks;
        },
        nb::arg("data")
    );
    module.def(
        "srt_to_sbt",
        [](const std::string& text, uint32_t language_id, uint32_t time_unit) {
            return to_python_bytes(unwrap_expected(cricodecs::usm::srt_to_sbt(text, language_id, time_unit)));
        },
        nb::arg("text"),
        nb::arg("language_id") = static_cast<uint32_t>(0),
        nb::arg("time_unit") = static_cast<uint32_t>(1000)
    );
    module.def(
        "sbt_to_ass",
        [](const nb::bytes& data, const std::string& title) {
            const auto view = borrow_python_bytes(data);
            return unwrap_expected(cricodecs::usm::sbt_to_ass(as_byte_span(view), title));
        },
        nb::arg("data"),
        nb::arg("title") = "CriCodecs subtitles"
    );
    module.def(
        "ass_to_sbt",
        [](const std::string& text, uint32_t language_id, uint32_t time_unit) {
            return to_python_bytes(unwrap_expected(cricodecs::usm::ass_to_sbt(text, language_id, time_unit)));
        },
        nb::arg("text"),
        nb::arg("language_id") = static_cast<uint32_t>(0),
        nb::arg("time_unit") = static_cast<uint32_t>(1000)
    );

    module.def(
        "load",
        &load_usm_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none(),
        nb::arg("key") = nb::none()
    );
    module.def(
        "recover_key",
        &recover_usm_python,
        nb::arg("source"),
        nb::arg("same_base_key") = true,
        nb::arg("encoding") = nb::none(),
        "Recover up to ten ranked USM key candidates from one or more movies."
    );
    module.def(
        "demux",
        [](cricodecs::usm::UsmReader& usm) {
            nb::dict streams;
            for (auto&& [name, bytes] : unwrap_expected(usm.demux())) {
                streams[nb::str(name.c_str())] =
                    to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
            }
            return streams;
        },
        nb::arg("source")
    );
    module.def(
        "demux",
        [](const nb::object& source, const nb::object& encoding, const nb::object& key) {
            auto usm = load_usm_any(source, encoding, key);
            nb::dict streams;
            for (auto&& [name, bytes] : unwrap_expected(usm.demux())) {
                streams[nb::str(name.c_str())] =
                    to_python_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()));
            }
            return streams;
        },
        nb::arg("source"),
        nb::arg("encoding") = nb::none(),
        nb::arg("key") = nb::none()
    );
    module.def(
        "extract",
        [](cricodecs::usm::UsmReader& usm, const nb::object& output_dir) {
            unwrap_expected(usm.extract(require_python_path(output_dir, "output_dir")));
        },
        nb::arg("source"),
        nb::arg("output_dir")
    );
    module.def(
        "extract",
        [](const nb::object& source, const nb::object& output_dir, const nb::object& encoding, const nb::object& key) {
            auto usm = load_usm_any(source, encoding, key);
            unwrap_expected(usm.extract(require_python_path(output_dir, "output_dir")));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("encoding") = nb::none(),
        nb::arg("key") = nb::none()
    );
}

} // namespace cricodecs::python
