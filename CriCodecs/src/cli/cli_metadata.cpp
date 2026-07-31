#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

namespace {

using MetadataScalar = std::variant<std::monostate, bool, uint64_t, int64_t, double, std::string>;

struct MetadataField {
    std::string key;
    MetadataScalar value;
};

using MetadataRow = std::vector<MetadataField>;

struct MetadataSection {
    std::string key;
    std::vector<MetadataRow> rows;
};

struct Metadata {
    MetadataRow fields;
    std::vector<MetadataSection> sections;
};

void add(MetadataRow& row, std::string_view key, std::string_view value) {
    row.push_back({std::string(key), std::string(value)});
}

void add(MetadataRow& row, std::string_view key, const std::string& value) {
    add(row, key, std::string_view(value));
}

void add(MetadataRow& row, std::string_view key, const char* value) {
    add(row, key, std::string_view(value));
}

void add(MetadataRow& row, std::string_view key, bool value) {
    row.push_back({std::string(key), value});
}

template <std::integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool>)
void add(MetadataRow& row, std::string_view key, T value) {
    if constexpr (std::signed_integral<T>) {
        row.push_back({std::string(key), static_cast<int64_t>(value)});
    } else {
        row.push_back({std::string(key), static_cast<uint64_t>(value)});
    }
}

void add(MetadataRow& row, std::string_view key, double value) {
    row.push_back({std::string(key), value});
}

void add_null(MetadataRow& row, std::string_view key) {
    row.push_back({std::string(key), std::monostate{}});
}

MetadataSection& add_section(Metadata& metadata, std::string_view key) {
    return metadata.sections.emplace_back(std::string(key), std::vector<MetadataRow>{});
}

std::string usm_stream_type_text(usm::UsmChunkType type) {
    const auto raw = static_cast<uint32_t>(type);
    std::string text(4, '\0');
    text[0] = static_cast<char>((raw >> 24) & 0xFFu);
    text[1] = static_cast<char>((raw >> 16) & 0xFFu);
    text[2] = static_cast<char>((raw >> 8) & 0xFFu);
    text[3] = static_cast<char>(raw & 0xFFu);
    return text;
}

std::string hca_version_text(uint16_t version) {
    return std::to_string(version >> 8) + "." + std::to_string(version & 0xFFu);
}

std::string hca_version_raw_text(uint16_t version) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << version;
    return stream.str();
}

std::string packed_version_text(uint32_t version) {
    if (version == 0) {
        return "0";
    }
    std::ostringstream stream;
    stream << ((version >> 24) & 0xFFu)
           << '.' << ((version >> 16) & 0xFFu)
           << '.' << ((version >> 8) & 0xFFu)
           << '.' << (version & 0xFFu);
    return stream.str();
}

std::string hex_u32_text(uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

std::string_view hca_codec_header_text(hca::HcaCodecChunkType type) {
    switch (type) {
    case hca::HcaCodecChunkType::Comp:
        return "comp";
    case hca::HcaCodecChunkType::Dec:
        return "dec";
    case hca::HcaCodecChunkType::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view hca_cipher_text(uint16_t type) {
    switch (type) {
    case 0:
        return "none";
    case 1:
        return "type-1 static";
    case 56:
        return "type-56 keyed";
    default:
        return "unknown";
    }
}

std::string_view adx_encoding_text(uint8_t mode) {
    switch (mode) {
    case 2:
        return "fixed-coefficient ADPCM";
    case 3:
        return "linear-prediction ADPCM";
    case 4:
        return "exponential-scale ADPCM";
    case 0x10:
    case 0x11:
        return "AHX";
    default:
        return "unknown";
    }
}

std::string_view adx_encryption_text(uint8_t type) {
    switch (type) {
    case 0:
        return "none";
    case 8:
        return "type-8 keystring";
    case 9:
        return "type-9 keycode";
    default:
        return "unknown";
    }
}

std::string_view sfd_variant_text(sfd::SfdHeaderVariant variant) {
    switch (variant) {
    case sfd::SfdHeaderVariant::sofdec_stream:
        return "SofdecStream";
    case sfd::SfdHeaderVariant::sofdec_stream2:
        return "SofdecStream2";
    case sfd::SfdHeaderVariant::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string sfd_version_text(const sfd::SfdHeaderSummary& summary) {
    std::ostringstream stream;
    for (uint8_t index = 0; index < summary.version_tag_size; ++index) {
        if (index != 0) {
            stream << '.';
        }
        stream << static_cast<unsigned int>(summary.version_tag_bytes[index]);
    }
    return stream.str();
}

std::string_view sfd_stream_type_text(sfd::SfdStreamType type) {
    switch (type) {
    case sfd::SfdStreamType::audio:
        return "audio";
    case sfd::SfdStreamType::video:
        return "video";
    case sfd::SfdStreamType::private_data:
        return "private_data";
    }
    return "unknown";
}

std::string_view sfd_audio_type_text(sfd::SfdAudioType type) {
    switch (type) {
    case sfd::SfdAudioType::adx:
        return "adx";
    case sfd::SfdAudioType::aix:
        return "aix";
    case sfd::SfdAudioType::ac3:
        return "ac3";
    case sfd::SfdAudioType::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view sfd_video_type_text(sfd::SfdVideoType type) {
    switch (type) {
    case sfd::SfdVideoType::mpeg1:
        return "mpeg1";
    case sfd::SfdVideoType::mpeg2:
        return "mpeg2";
    case sfd::SfdVideoType::unknown:
        return "unknown";
    }
    return "unknown";
}

void print_scalar_text(std::ostream& out, const MetadataScalar& value) {
    std::visit([&out](const auto& current) {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::same_as<T, std::monostate>) {
            out << "null";
        } else if constexpr (std::same_as<T, bool>) {
            out << bool_text(current);
        } else {
            out << current;
        }
    }, value);
}

void print_scalar_json(std::ostream& out, const MetadataScalar& value) {
    std::visit([&out](const auto& current) {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::same_as<T, std::monostate>) {
            out << "null";
        } else if constexpr (std::same_as<T, std::string>) {
            out << quote_json(current);
        } else if constexpr (std::same_as<T, bool>) {
            out << bool_text(current);
        } else {
            out << current;
        }
    }, value);
}

void print_metadata_text_model(std::ostream& out, const Metadata& metadata) {
    for (const auto& field : metadata.fields) {
        out << field.key << ": ";
        print_scalar_text(out, field.value);
        out << '\n';
    }
    for (const auto& section : metadata.sections) {
        out << section.key << ":\n";
        for (size_t index = 0; index < section.rows.size(); ++index) {
            const auto& row = section.rows[index];
            if (row.empty()) {
                out << "  - {}\n";
                continue;
            }
            out << "  - " << row.front().key << ": ";
            print_scalar_text(out, row.front().value);
            out << '\n';
            for (size_t field_index = 1; field_index < row.size(); ++field_index) {
                out << "    " << row[field_index].key << ": ";
                print_scalar_text(out, row[field_index].value);
                out << '\n';
            }
        }
    }
}

void print_metadata_json_model(std::ostream& out, const Metadata& metadata) {
    out << '{';
    bool first = true;
    const auto separator = [&out, &first] {
        if (!first) {
            out << ',';
        }
        first = false;
    };
    for (const auto& field : metadata.fields) {
        separator();
        out << quote_json(field.key) << ':';
        print_scalar_json(out, field.value);
    }
    for (const auto& section : metadata.sections) {
        separator();
        out << quote_json(section.key) << ":[";
        for (size_t index = 0; index < section.rows.size(); ++index) {
            if (index != 0) {
                out << ',';
            }
            out << '{';
            for (size_t field_index = 0; field_index < section.rows[index].size(); ++field_index) {
                if (field_index != 0) {
                    out << ',';
                }
                const auto& field = section.rows[index][field_index];
                out << quote_json(field.key) << ':';
                print_scalar_json(out, field.value);
            }
            out << '}';
        }
        out << ']';
    }
    out << '}';
}

Metadata build_metadata(Format format, const LoadedDocument& document) {
    Metadata metadata;
    add(metadata.fields, "format", format_key(format));

    std::visit([&metadata](const auto& current) {
        using T = std::decay_t<decltype(current)>;

        if constexpr (std::same_as<T, adx::Adx>) {
            const auto& header = current.header();
            add(metadata.fields, "input_type", current.is_ahx() ? "ahx" : "adx");
            add(metadata.fields, "version", header.version);
            add(metadata.fields, "encoding", adx_encoding_text(header.encoding_mode));
            add(metadata.fields, "encoding_mode", header.encoding_mode);
            add(metadata.fields, "block_size", header.block_size);
            add(metadata.fields, "bit_depth", header.bit_depth);
            add(metadata.fields, "channels", header.channels);
            add(metadata.fields, "sample_rate", header.sample_rate);
            add(metadata.fields, "sample_count", header.sample_count);
            add(metadata.fields, "highpass_frequency", header.highpass_freq);
            add(metadata.fields, "encrypted", current.is_encrypted());
            add(metadata.fields, "encryption_type", header.flags);
            add(metadata.fields, "encryption", adx_encryption_text(header.flags));
            add(metadata.fields, "loop_count", current.loops().size());
            if (!current.loops().empty()) {
                auto& loops = add_section(metadata, "loops");
                for (const auto& loop : current.loops()) {
                    auto& row = loops.rows.emplace_back();
                    add(row, "index", loop.index);
                    add(row, "type", loop.type);
                    add(row, "start_sample", loop.start_sample);
                    add(row, "end_sample", loop.end_sample);
                    add(row, "start_byte", loop.start_byte);
                    add(row, "end_byte", loop.end_byte);
                }
            }
        } else if constexpr (std::same_as<T, hca::Hca>) {
            const auto& header = current.header();
            add(metadata.fields, "version", hca_version_text(header.file.version));
            add(metadata.fields, "version_raw", hca_version_raw_text(header.file.version));
            add(metadata.fields, "codec_header", hca_codec_header_text(header.codec.type()));
            add(metadata.fields, "channels", header.fmt.channel_count);
            add(metadata.fields, "tracks", header.codec.track_count);
            add(metadata.fields, "channel_config", header.codec.channel_config);
            add(metadata.fields, "sample_rate", header.fmt.sample_rate);
            add(metadata.fields, "sample_count", header.sample_count());
            add(metadata.fields, "frame_count", header.fmt.frame_count);
            add(metadata.fields, "frame_size", header.codec.frame_size);
            add(metadata.fields, "encoder_delay", header.fmt.encoder_delay);
            add(metadata.fields, "encoder_padding", header.fmt.encoder_padding);
            add(metadata.fields, "vbr", header.vbr.enabled());
            add(metadata.fields, "ath_type", header.ath.type);
            add(metadata.fields, "volume", static_cast<double>(header.rva.volume));
            add(metadata.fields, "encrypted", header.cipher.encrypted());
            add(metadata.fields, "cipher_type", header.cipher.type);
            add(metadata.fields, "encryption", hca_cipher_text(header.cipher.type));
            add(metadata.fields, "loop_count", header.loop.enabled() ? 1u : 0u);
            if (header.loop.enabled()) {
                const uint64_t start_encoded =
                    static_cast<uint64_t>(header.loop.start_frame) * hca::HCA_SAMPLES_PER_FRAME +
                    header.loop.start_delay;
                const uint64_t end_encoded =
                    (static_cast<uint64_t>(header.loop.end_frame) + 1u) * hca::HCA_SAMPLES_PER_FRAME -
                    header.loop.end_padding;
                auto& loops = add_section(metadata, "loops");
                auto& row = loops.rows.emplace_back();
                add(row, "index", 0u);
                add(row, "start_sample",
                    start_encoded >= header.fmt.encoder_delay ? start_encoded - header.fmt.encoder_delay : 0u);
                add(row, "end_sample",
                    end_encoded >= header.fmt.encoder_delay ? end_encoded - header.fmt.encoder_delay : 0u);
                add(row, "start_frame", header.loop.start_frame);
                add(row, "end_frame", header.loop.end_frame);
                add(row, "start_delay", header.loop.start_delay);
                add(row, "end_padding", header.loop.end_padding);
            }
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            add(metadata.fields, "name", current.name());
            add(metadata.fields, "segment_count", current.segment_count());
            add(metadata.fields, "channels", current.channels());
            add(metadata.fields, "sample_rate", current.sample_rate());
            add(metadata.fields, "sample_count", current.sample_count());
            add(metadata.fields, "has_loop_segments", current.has_loop_segments());
            auto& segments = add_section(metadata, "segments");
            for (const auto& segment : current.segments()) {
                auto& row = segments.rows.emplace_back();
                add(row, "index", segment.row_index);
                add(row, "sample_count", segment.sample_count);
                add(row, "data_size", segment.data_size);
                add(row, "loop", segment.loop_segment);
            }
        } else if constexpr (std::same_as<T, aix::Aix>) {
            add(metadata.fields, "version", "1.0.0.20");
            add(metadata.fields, "segment_count", current.segments().size());
            add(metadata.fields, "layer_count", current.layers().size());
            add(metadata.fields, "total_sample_count", current.total_sample_count());
            add(metadata.fields, "has_inferred_loop", current.inferred_loop().has_value());
            if (current.inferred_loop()) {
                auto& loops = add_section(metadata, "loops");
                auto& row = loops.rows.emplace_back();
                add(row, "start_segment", current.inferred_loop()->start_segment);
                add(row, "end_segment", current.inferred_loop()->end_segment);
                add(row, "start_sample", current.inferred_loop()->start_sample);
                add(row, "end_sample", current.inferred_loop()->end_sample);
            }
            auto& layers = add_section(metadata, "layers");
            for (size_t index = 0; index < current.layers().size(); ++index) {
                auto& row = layers.rows.emplace_back();
                add(row, "index", index);
                add(row, "channels", current.layers()[index].channel_count);
                add(row, "sample_rate", current.layers()[index].sample_rate);
            }
        } else if constexpr (std::same_as<T, acx::AcxContainer>) {
            add(metadata.fields, "entry_count", current.entry_count());
            add(metadata.fields, "adx_entries", current.type_count(acx::AcxEntryType::adx));
            add(metadata.fields, "ogg_entries", current.type_count(acx::AcxEntryType::ogg));
            add(metadata.fields, "unknown_entries", current.type_count(acx::AcxEntryType::unknown));
            if (const auto first = current.first_payload_offset()) {
                add(metadata.fields, "first_payload_offset", *first);
            }
        } else if constexpr (std::same_as<T, afs::AfsContainer>) {
            add(metadata.fields, "entry_count", current.entry_count());
            add(metadata.fields, "present_entry_count", current.present_entry_count());
            add(metadata.fields, "alignment", current.alignment());
            add(metadata.fields, "has_directory_table", current.has_directory_table());
            if (const auto first = current.first_payload_offset()) {
                add(metadata.fields, "first_payload_offset", *first);
            }
        } else if constexpr (std::same_as<T, awb::AwbContainer>) {
            add(metadata.fields, "file_count", current.file_count());
            add(metadata.fields, "version", current.version());
            add(metadata.fields, "id_size", current.id_size());
            add(metadata.fields, "offset_size", current.offset_size());
            add(metadata.fields, "alignment", current.alignment());
            add(metadata.fields, "subkey", current.subkey());

            std::map<awb::EntryCodec, uint64_t> codec_counts;
            for (uint32_t index = 0; index < current.file_count(); ++index) {
                const auto codec = current.entry_codec(index);
                ++codec_counts[codec ? *codec : awb::EntryCodec::Unknown];
            }
            auto& codecs = add_section(metadata, "codecs");
            for (const auto& [codec, count] : codec_counts) {
                auto& row = codecs.rows.emplace_back();
                add(row, "codec", awb::entry_codec_name(codec));
                add(row, "extension", awb::entry_codec_extension(codec));
                add(row, "count", count);
            }
        } else if constexpr (std::same_as<T, acb::AcbContainer>) {
            add(metadata.fields, "name", current.name());
            if (const auto version = current.header_table().template get<uint32_t>(0, "Version")) {
                add(metadata.fields, "version", packed_version_text(*version));
                add(metadata.fields, "version_raw", hex_u32_text(*version));
            }
            add(metadata.fields, "cue_count", current.cue_graph().cues().size());
            add(metadata.fields, "cue_name_count", current.cue_graph().cue_names().size());
            if (const auto resolution = acb::resolve_cue_sheet_playback(current)) {
                add(metadata.fields, "resolved_cue_plan_count", resolution->plans.size());
                add(metadata.fields, "non_playable_cue_count", resolution->non_playable_cues.size());
            } else {
                add_null(metadata.fields, "resolved_cue_plan_count");
                add_null(metadata.fields, "non_playable_cue_count");
            }
            add(metadata.fields, "waveform_count", current.waveform_count());
            add(metadata.fields, "has_embedded_awb", current.has_embedded_awb());
            add(metadata.fields, "has_companion_awb", current.companion_awb_path().has_value());

            uint64_t loop_count = 0;
            uint64_t memory_count = 0;
            uint64_t streaming_count = 0;
            std::map<std::string, uint64_t> codec_counts;
            for (uint32_t index = 0; index < current.waveform_count(); ++index) {
                const auto& waveform = current.waveform(index);
                loop_count += waveform.loop_flag ? 1u : 0u;
                memory_count += waveform.streaming == 0 ? 1u : 0u;
                streaming_count += waveform.streaming == 0 ? 0u : 1u;
                const auto codec = current.waveform_codec(index);
                if (codec) {
                    ++codec_counts[std::string(awb::entry_codec_name(*codec))];
                } else {
                    ++codec_counts["unknown audio"];
                }
            }
            add(metadata.fields, "looping_waveform_count", loop_count);
            add(metadata.fields, "memory_waveform_count", memory_count);
            add(metadata.fields, "streaming_waveform_count", streaming_count);
            auto& codecs = add_section(metadata, "codecs");
            for (const auto& [codec, count] : codec_counts) {
                auto& row = codecs.rows.emplace_back();
                add(row, "codec", codec);
                add(row, "count", count);
            }
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            add(metadata.fields, "file_count", current.file_count());
            add(metadata.fields, "alignment", current.alignment());
            add(metadata.fields, "content_offset", current.content_offset());
            add(metadata.fields, "utf_version", current.cpk_header().version());
            add(metadata.fields, "has_toc", current.has_toc());
            add(metadata.fields, "has_itoc", current.has_itoc());
            add(metadata.fields, "has_gtoc", current.has_gtoc());
            add(metadata.fields, "has_etoc", current.has_etoc());
        } else if constexpr (std::same_as<T, csb::CsbContainer>) {
            add(metadata.fields, "name", current.name());
            add(metadata.fields, "section_count", current.section_count());
            add(metadata.fields, "element_count", current.element_count());
            add(metadata.fields, "stream_count", current.stream_count());
            auto& streams = add_section(metadata, "streams");
            for (uint32_t index = 0; index < current.stream_count(); ++index) {
                const auto& stream = current.stream(index);
                auto& row = streams.rows.emplace_back();
                add(row, "index", index);
                add(row, "name", stream.name);
                add(row, "format", stream.wrapper_table_name);
                add(row, "extension", csb::stream_file_extension(stream.format));
                add(row, "channels", stream.channels);
                add(row, "sample_rate", stream.sample_rate);
                add(row, "sample_count", stream.sample_count);
                add(row, "streamed", stream.streamed);
            }
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            add(metadata.fields, "disc_name", current.disc_name());
            add(metadata.fields, "media", current.media());
            add(metadata.fields, "recording_date", current.recording_date_text());
            add(metadata.fields, "filesystem_id", current.header().filesystem_id);
            add(metadata.fields, "maker_id", current.header().maker_id);
            add(metadata.fields, "entry_count", current.entry_count());
            add(metadata.fields, "scrambled", current.is_scrambled());
            add(metadata.fields, "has_accessible_contents", current.has_accessible_contents());
            add(metadata.fields, "embedded_iso_offset", current.embedded_iso_offset());
            add(metadata.fields, "embedded_iso_size", current.embedded_iso_size());
            add(metadata.fields, "embedded_iso_sector_count", current.embedded_iso_sector_count());
            add(metadata.fields, "logical_block_size", current.primary_volume().logical_block_size);
            add(metadata.fields, "volume_identifier", current.primary_volume().volume_identifier);
            add(metadata.fields, "application_identifier", current.primary_volume().application_identifier);
        } else if constexpr (std::same_as<T, sfd::SfdContainer>) {
            add(metadata.fields, "stream_count", current.stream_count());
            add(metadata.fields, "has_header_summary", current.header_summary().has_value());
            if (current.header_summary()) {
                const auto& summary = *current.header_summary();
                add(metadata.fields, "variant", sfd_variant_text(summary.variant));
                add(metadata.fields, "version", sfd_version_text(summary));
                add(metadata.fields, "builder_version", summary.builder_version);
                add(metadata.fields, "pack_size", summary.pack_size);
                add(metadata.fields, "variable_pack", summary.variable_pack);
                add(metadata.fields, "reserved_header_size", summary.reserved_header_size);
                add(metadata.fields, "element_count", summary.element_count);
                add(metadata.fields, "audio_count", summary.audio_count);
                add(metadata.fields, "video_count", summary.video_count);
                add(metadata.fields, "private_count", summary.private_count);
                add(metadata.fields, "output_name", summary.output_name);
            }
            auto& streams = add_section(metadata, "streams");
            for (const auto& stream : current.streams()) {
                auto& row = streams.rows.emplace_back();
                add(row, "index", stream.index);
                add(row, "type", sfd_stream_type_text(stream.type));
                add(row, "type_index", stream.type_index);
                add(row, "stream_id", hex_text(stream.stream_id));
                if (stream.type == sfd::SfdStreamType::audio) {
                    add(row, "codec", sfd_audio_type_text(stream.audio_type));
                } else if (stream.type == sfd::SfdStreamType::video) {
                    add(row, "codec", sfd_video_type_text(stream.video_type));
                } else {
                    add(row, "codec", "unknown");
                }
                add(row, "source_name", stream.source_name);
                add(row, "packet_count", stream.packet_count);
                add(row, "extracted_size", stream.extracted_size);
                if (stream.video_header) {
                    add(row, "width", stream.video_header->width);
                    add(row, "height", stream.video_header->height);
                    add(row, "frame_rate_code", stream.video_header->frame_rate_code);
                    add(row, "aspect_ratio_code", stream.video_header->aspect_ratio_code);
                    add(row, "bit_rate_value", stream.video_header->bit_rate_value);
                }
                if (stream.element_record) {
                    if (stream.element_record->audio_channels) {
                        add(row, "channels", *stream.element_record->audio_channels);
                    }
                    if (stream.element_record->audio_sample_rate) {
                        add(row, "sample_rate", *stream.element_record->audio_sample_rate);
                    }
                }
            }
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            add(metadata.fields, "container_filename", current.container_filename());
            add(metadata.fields, "stream_count", current.streams().size());
            add(metadata.fields, "chunk_count", current.chunks().size());
            add(metadata.fields, "variant", current.sfsh_header() ? "SFSH" : "CRID");
            add(metadata.fields, "has_sfsh", current.sfsh_header().has_value());
            if (current.sfsh_header()) {
                add(metadata.fields, "version", current.sfsh_header()->version);
                add(metadata.fields, "payload_size", current.sfsh_header()->payload_size);
                add(metadata.fields, "codec_marker", current.sfsh_header()->codec_marker());
                add(metadata.fields, "normalized_codec_marker", current.sfsh_header()->normalized_codec_marker());
            } else {
                add(metadata.fields, "crid_table", current.crid_header().table_name());
                add(metadata.fields, "crid_table_version", current.crid_header().version());
            }
            auto& streams = add_section(metadata, "streams");
            for (size_t index = 0; index < current.streams().size(); ++index) {
                const auto& stream = current.streams()[index];
                auto& row = streams.rows.emplace_back();
                add(row, "index", index);
                add(row, "type", usm_stream_type_text(stream.stream_id));
                add(row, "channel", stream.channel_no);
                add(row, "filename", stream.filename);
                if (stream.audio_codec) {
                    add(row, "audio_codec", usm::audio_codec_name(*stream.audio_codec));
                } else {
                    add_null(row, "audio_codec");
                }
                add(row, "format_version", packed_version_text(stream.fmtver));
                add(row, "format_version_raw", hex_u32_text(stream.fmtver));
                add(row, "file_size", stream.filesize);
                add(row, "minimum_chunk_size", stream.minchk);
                add(row, "minimum_buffer_size", stream.minbuf);
                add(row, "average_bitrate", stream.avbps);

                if (stream.audio_codec &&
                    index <= std::numeric_limits<uint32_t>::max()) {
                    const auto sample = current.extract_stream_sample(
                        static_cast<uint32_t>(index),
                        64u * 1024u);
                    if (sample && *stream.audio_codec == usm::UsmAudioCodec::Hca) {
                        if (const auto audio = hca::Hca::load(std::span<const uint8_t>(*sample))) {
                            add(row, "codec_version", hca_version_text(audio->header().file.version));
                            add(row, "cipher_type", audio->header().cipher.type);
                            add(row, "encryption", hca_cipher_text(audio->header().cipher.type));
                            add(row, "loop_count", audio->header().loop.enabled() ? 1u : 0u);
                        }
                    } else if (sample && *stream.audio_codec == usm::UsmAudioCodec::Adx) {
                        if (const auto audio = adx::Adx::load(std::span<const uint8_t>(*sample))) {
                            add(row, "codec_version", audio->header().version);
                            add(row, "encryption_type", audio->header().flags);
                            add(row, "encryption", adx_encryption_text(audio->header().flags));
                            add(row, "loop_count", audio->loops().size());
                        }
                    }
                }
            }
        } else if constexpr (std::same_as<T, utf::UtfTable>) {
            // UTF keeps its richer schema/row renderer below.
        }
    }, document);

    return metadata;
}

void print_value_json(std::ostream& out, const utf::Value& value);

void print_utf_rows_json(std::ostream& out, const utf::UtfTable& table) {
    out << "\"rows\":[";
    for (uint32_t row = 0; row < table.row_count(); ++row) {
        if (row != 0) {
            out << ',';
        }
        out << '{';
        bool first = true;
        for (uint32_t column = 0; column < table.column_count(); ++column) {
            auto value = table.get_value(row, column);
            if (!value) {
                continue;
            }
            if (!first) {
                out << ',';
            }
            first = false;
            out << quote_json(table.column(column).name) << ':';
            print_value_json(out, *value);
        }
        out << '}';
    }
    out << ']';
}

void print_value_json(std::ostream& out, const utf::Value& value) {
    std::visit([&out](const auto& current) {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::same_as<T, std::monostate>) {
            out << "null";
        } else if constexpr (std::same_as<T, std::string>) {
            out << quote_json(current);
        } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
            out << '[';
            for (size_t index = 0; index < current.size(); ++index) {
                if (index != 0) {
                    out << ',';
                }
                out << static_cast<unsigned int>(current[index]);
            }
            out << ']';
        } else if constexpr (std::same_as<T, utf::DataRef>) {
            out << "{\"offset\":" << current.offset << ",\"size\":" << current.size << '}';
        } else if constexpr (std::same_as<T, utf::GUID>) {
            std::ostringstream stream;
            for (uint8_t byte : current.data) {
                stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
            }
            out << quote_json(stream.str());
        } else if constexpr (std::same_as<T, bool>) {
            out << bool_text(current);
        } else if constexpr (std::integral<T> && sizeof(T) == 1) {
            if constexpr (std::signed_integral<T>) {
                out << static_cast<int>(current);
            } else {
                out << static_cast<unsigned int>(current);
            }
        } else {
            out << current;
        }
    }, value);
}

void print_utf_text(std::ostream& out, const utf::UtfTable& table) {
    out << "format: utf\n";
    out << "table_name: " << table.table_name() << '\n';
    out << "version: " << table.version() << '\n';
    out << "row_count: " << table.row_count() << '\n';
    out << "column_count: " << table.column_count() << '\n';
    out << "table_size: " << table.table_size() << '\n';
    out << "row_width: " << table.row_width() << '\n';
    out << "columns:\n";
    for (uint32_t index = 0; index < table.column_count(); ++index) {
        const auto& column = table.column(index);
        out << "  - name: " << column.name
            << "\n    type: " << static_cast<unsigned int>(column.type)
            << "\n    flag_bits: " << static_cast<unsigned int>(column.flag)
            << '\n';
    }
    out << "rows:\n";
    for (uint32_t row = 0; row < table.row_count(); ++row) {
        out << "  - index: " << row << '\n';
        for (uint32_t column = 0; column < table.column_count(); ++column) {
            auto value = table.get_value(row, column);
            if (!value) {
                out << "    " << table.column(column).name << ": <error: " << value.error() << ">\n";
                continue;
            }

            std::ostringstream rendered;
            std::visit([&rendered](const auto& current) {
                using T = std::decay_t<decltype(current)>;
                if constexpr (std::same_as<T, std::monostate>) {
                    rendered << "null";
                } else if constexpr (std::same_as<T, std::string>) {
                    rendered << current;
                } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
                    rendered << "<bytes:" << current.size() << '>';
                } else if constexpr (std::same_as<T, utf::DataRef>) {
                    rendered << "{offset=" << current.offset << ", size=" << current.size << '}';
                } else if constexpr (std::same_as<T, utf::GUID>) {
                    rendered << hex_text(current.data[0]);
                } else if constexpr (std::same_as<T, bool>) {
                    rendered << bool_text(current);
                } else if constexpr (std::integral<T> && sizeof(T) == 1) {
                    if constexpr (std::signed_integral<T>) {
                        rendered << static_cast<int>(current);
                    } else {
                        rendered << static_cast<unsigned int>(current);
                    }
                } else {
                    rendered << current;
                }
            }, *value);
            out << "    " << table.column(column).name << ": " << rendered.str() << '\n';
        }
    }
}

void print_utf_json(std::ostream& out, const utf::UtfTable& table) {
    out << '{'
        << "\"format\":\"utf\""
        << ",\"table_name\":" << quote_json(table.table_name())
        << ",\"version\":" << table.version()
        << ",\"row_count\":" << table.row_count()
        << ",\"column_count\":" << table.column_count()
        << ",\"table_size\":" << table.table_size()
        << ",\"row_width\":" << table.row_width()
        << ",\"columns\":[";
    for (uint32_t index = 0; index < table.column_count(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto& column = table.column(index);
        out << '{'
            << "\"name\":" << quote_json(column.name)
            << ",\"type\":" << static_cast<unsigned int>(column.type)
            << ",\"flag_bits\":" << static_cast<unsigned int>(column.flag)
            << '}';
    }
    out << "],";
    print_utf_rows_json(out, table);
    out << '}';
}

} // namespace

void print_metadata_text(std::ostream& out, Format format, const LoadedDocument& document) {
    if (const auto* table = std::get_if<utf::UtfTable>(&document)) {
        print_utf_text(out, *table);
        return;
    }
    print_metadata_text_model(out, build_metadata(format, document));
}

void print_metadata_json(std::ostream& out, Format format, const LoadedDocument& document) {
    if (const auto* table = std::get_if<utf::UtfTable>(&document)) {
        print_utf_json(out, *table);
        return;
    }
    print_metadata_json_model(out, build_metadata(format, document));
}

} // namespace cricodecs::cli::detail
