#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

namespace {

std::string usm_stream_type_text(usm::UsmChunkType type) {
    const auto raw = static_cast<uint32_t>(type);
    std::string text(4, '\0');
    text[0] = static_cast<char>((raw >> 24) & 0xFFu);
    text[1] = static_cast<char>((raw >> 16) & 0xFFu);
    text[2] = static_cast<char>((raw >> 8) & 0xFFu);
    text[3] = static_cast<char>(raw & 0xFFu);
    return text;
}

} // namespace

template <typename T>
void print_line(std::ostream& out, std::string_view key, const T& value) {
    out << key << ": " << value << '\n';
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
            out << (current ? "true" : "false");
        } else {
            out << current;
        }
    }, value);
}

void print_utf_text(std::ostream& out, const utf::UtfTable& table) {
    print_line(out, "format", "utf");
    print_line(out, "table_name", std::string(table.table_name()));
    print_line(out, "version", table.version());
    print_line(out, "row_count", table.row_count());
    print_line(out, "column_count", table.column_count());
    print_line(out, "table_size", table.table_size());
    print_line(out, "row_width", table.row_width());
    out << "columns:\n";
    for (uint32_t index = 0; index < table.column_count(); ++index) {
        const auto& column = table.column(index);
        out << "  - " << column.name
            << " type=" << static_cast<unsigned int>(column.type)
            << " flag_bits=" << static_cast<unsigned int>(column.flag)
            << '\n';
    }
    out << "rows:\n";
    for (uint32_t row = 0; row < table.row_count(); ++row) {
        out << "  [" << row << "]\n";
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
                } else {
                    rendered << current;
                }
            }, *value);
            out << "    " << table.column(column).name << ": " << rendered.str() << '\n';
        }
    }
}

void print_metadata_text(std::ostream& out, Format format, const LoadedDocument& document) {
    std::visit([&out, format](const auto& current) {
        using T = std::decay_t<decltype(current)>;
        print_line(out, "format", std::string(format_key(format)));

        if constexpr (std::same_as<T, adx::Adx>) {
            const auto& header = current.header();
            print_line(out, "input_type", current.is_ahx() ? "ahx" : "adx");
            print_line(out, "channels", static_cast<unsigned int>(header.channels));
            print_line(out, "sample_rate", header.sample_rate);
            print_line(out, "sample_count", header.sample_count);
            print_line(out, "encrypted", bool_text(current.is_encrypted()));
            print_line(out, "loop_count", current.loops().size());
        } else if constexpr (std::same_as<T, hca::Hca>) {
            const auto& header = current.header();
            print_line(out, "version", header.file.version);
            print_line(out, "channels", static_cast<unsigned int>(header.fmt.channel_count));
            print_line(out, "sample_rate", header.fmt.sample_rate);
            print_line(out, "sample_count", header.sample_count());
            print_line(out, "encrypted", bool_text(header.cipher.encrypted()));
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            print_line(out, "name", std::string(current.name()));
            print_line(out, "segment_count", current.segment_count());
            print_line(out, "channels", static_cast<unsigned int>(current.channels()));
            print_line(out, "sample_rate", current.sample_rate());
            print_line(out, "sample_count", current.sample_count());
            print_line(out, "has_loop_segments", bool_text(current.has_loop_segments()));
        } else if constexpr (std::same_as<T, aix::Aix>) {
            print_line(out, "segment_count", current.segments().size());
            print_line(out, "layer_count", current.layers().size());
            print_line(out, "total_sample_count", current.total_sample_count());
            print_line(out, "has_inferred_loop", bool_text(current.inferred_loop().has_value()));
        } else if constexpr (std::same_as<T, acx::AcxContainer>) {
            print_line(out, "entry_count", current.entry_count());
            print_line(out, "adx_entries", current.type_count(acx::AcxEntryType::adx));
            print_line(out, "ogg_entries", current.type_count(acx::AcxEntryType::ogg));
            if (const auto first = current.first_payload_offset()) {
                print_line(out, "first_payload_offset", *first);
            }
        } else if constexpr (std::same_as<T, afs::AfsContainer>) {
            print_line(out, "entry_count", current.entry_count());
            print_line(out, "present_entry_count", current.present_entry_count());
            print_line(out, "alignment", current.alignment());
            print_line(out, "has_directory_table", bool_text(current.has_directory_table()));
            if (const auto first = current.first_payload_offset()) {
                print_line(out, "first_payload_offset", *first);
            }
        } else if constexpr (std::same_as<T, awb::AwbContainer>) {
            print_line(out, "file_count", current.file_count());
            print_line(out, "version", static_cast<unsigned int>(current.version()));
            print_line(out, "alignment", current.alignment());
            print_line(out, "subkey", current.subkey());
        } else if constexpr (std::same_as<T, acb::AcbContainer>) {
            print_line(out, "name", std::string(current.name()));
            print_line(out, "waveform_count", current.waveform_count());
            print_line(out, "has_embedded_awb", bool_text(current.has_embedded_awb()));
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            print_line(out, "file_count", current.file_count());
            print_line(out, "alignment", current.alignment());
            print_line(out, "content_offset", current.content_offset());
            print_line(out, "has_toc", bool_text(current.has_toc()));
            print_line(out, "has_itoc", bool_text(current.has_itoc()));
            print_line(out, "has_gtoc", bool_text(current.has_gtoc()));
            print_line(out, "has_etoc", bool_text(current.has_etoc()));
        } else if constexpr (std::same_as<T, csb::CsbContainer>) {
            print_line(out, "name", std::string(current.name()));
            print_line(out, "section_count", current.section_count());
            print_line(out, "element_count", current.element_count());
            print_line(out, "stream_count", current.stream_count());
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            print_line(out, "disc_name", current.disc_name());
            print_line(out, "entry_count", current.entry_count());
            print_line(out, "is_scrambled", bool_text(current.is_scrambled()));
            print_line(out, "has_accessible_contents", bool_text(current.has_accessible_contents()));
            print_line(out, "embedded_iso_size", current.embedded_iso_size());
        } else if constexpr (std::same_as<T, sfd::SfdContainer>) {
            print_line(out, "stream_count", current.stream_count());
            print_line(out, "has_header_summary", bool_text(current.header_summary().has_value()));
            if (current.header_summary().has_value()) {
                print_line(out, "audio_count", current.header_summary()->audio_count);
                print_line(out, "video_count", current.header_summary()->video_count);
            }
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            print_line(out, "container_filename", std::string(current.container_filename()));
            print_line(out, "stream_count", current.streams().size());
            print_line(out, "has_sfsh", bool_text(current.sfsh_header().has_value()));
            print_line(out, "crid_table", std::string(current.crid_header().table_name()));
            out << "streams:\n";
            for (size_t index = 0; index < current.streams().size(); ++index) {
                const auto& stream = current.streams()[index];
                out << "  [" << index << "] type=" << usm_stream_type_text(stream.stream_id)
                    << " channel=" << static_cast<unsigned int>(stream.channel_no)
                    << " filename=" << stream.filename;
                if (stream.audio_codec.has_value()) {
                    out << " audio_codec=" << usm::audio_codec_name(*stream.audio_codec);
                }
                out << '\n';
            }
        } else if constexpr (std::same_as<T, utf::UtfTable>) {
            print_utf_text(out, current);
        }
    }, document);
}

void print_metadata_json(std::ostream& out, Format format, const LoadedDocument& document) {
    std::visit([&out, format](const auto& current) {
        using T = std::decay_t<decltype(current)>;
        out << '{';
        out << "\"format\":" << quote_json(format_key(format));

        if constexpr (std::same_as<T, adx::Adx>) {
            const auto& header = current.header();
            out << ",\"input_type\":" << quote_json(current.is_ahx() ? "ahx" : "adx")
                << ",\"channels\":" << static_cast<unsigned int>(header.channels)
                << ",\"sample_rate\":" << header.sample_rate
                << ",\"sample_count\":" << header.sample_count
                << ",\"encrypted\":" << bool_text(current.is_encrypted())
                << ",\"loop_count\":" << current.loops().size();
        } else if constexpr (std::same_as<T, hca::Hca>) {
            const auto& header = current.header();
            out << ",\"version\":" << header.file.version
                << ",\"channels\":" << static_cast<unsigned int>(header.fmt.channel_count)
                << ",\"sample_rate\":" << header.fmt.sample_rate
                << ",\"sample_count\":" << header.sample_count()
                << ",\"encrypted\":" << bool_text(header.cipher.encrypted());
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            out << ",\"name\":" << quote_json(current.name())
                << ",\"segment_count\":" << current.segment_count()
                << ",\"channels\":" << static_cast<unsigned int>(current.channels())
                << ",\"sample_rate\":" << current.sample_rate()
                << ",\"sample_count\":" << current.sample_count()
                << ",\"has_loop_segments\":" << bool_text(current.has_loop_segments());
        } else if constexpr (std::same_as<T, aix::Aix>) {
            out << ",\"segment_count\":" << current.segments().size()
                << ",\"layer_count\":" << current.layers().size()
                << ",\"total_sample_count\":" << current.total_sample_count()
                << ",\"has_inferred_loop\":" << bool_text(current.inferred_loop().has_value());
        } else if constexpr (std::same_as<T, acx::AcxContainer>) {
            out << ",\"entry_count\":" << current.entry_count()
                << ",\"adx_entries\":" << current.type_count(acx::AcxEntryType::adx)
                << ",\"ogg_entries\":" << current.type_count(acx::AcxEntryType::ogg);
            if (const auto first = current.first_payload_offset()) {
                out << ",\"first_payload_offset\":" << *first;
            }
        } else if constexpr (std::same_as<T, afs::AfsContainer>) {
            out << ",\"entry_count\":" << current.entry_count()
                << ",\"present_entry_count\":" << current.present_entry_count()
                << ",\"alignment\":" << current.alignment()
                << ",\"has_directory_table\":" << bool_text(current.has_directory_table());
            if (const auto first = current.first_payload_offset()) {
                out << ",\"first_payload_offset\":" << *first;
            }
        } else if constexpr (std::same_as<T, awb::AwbContainer>) {
            out << ",\"file_count\":" << current.file_count()
                << ",\"version\":" << static_cast<unsigned int>(current.version())
                << ",\"alignment\":" << current.alignment()
                << ",\"subkey\":" << current.subkey();
        } else if constexpr (std::same_as<T, acb::AcbContainer>) {
            out << ",\"name\":" << quote_json(current.name())
                << ",\"waveform_count\":" << current.waveform_count()
                << ",\"has_embedded_awb\":" << bool_text(current.has_embedded_awb());
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            out << ",\"file_count\":" << current.file_count()
                << ",\"alignment\":" << current.alignment()
                << ",\"content_offset\":" << current.content_offset()
                << ",\"has_toc\":" << bool_text(current.has_toc())
                << ",\"has_itoc\":" << bool_text(current.has_itoc())
                << ",\"has_gtoc\":" << bool_text(current.has_gtoc())
                << ",\"has_etoc\":" << bool_text(current.has_etoc());
        } else if constexpr (std::same_as<T, csb::CsbContainer>) {
            out << ",\"name\":" << quote_json(current.name())
                << ",\"section_count\":" << current.section_count()
                << ",\"element_count\":" << current.element_count()
                << ",\"stream_count\":" << current.stream_count();
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            out << ",\"disc_name\":" << quote_json(current.disc_name())
                << ",\"entry_count\":" << current.entry_count()
                << ",\"is_scrambled\":" << bool_text(current.is_scrambled())
                << ",\"has_accessible_contents\":" << bool_text(current.has_accessible_contents())
                << ",\"embedded_iso_size\":" << current.embedded_iso_size();
        } else if constexpr (std::same_as<T, sfd::SfdContainer>) {
            out << ",\"stream_count\":" << current.stream_count()
                << ",\"has_header_summary\":" << bool_text(current.header_summary().has_value());
            if (current.header_summary().has_value()) {
                out << ",\"audio_count\":" << current.header_summary()->audio_count
                    << ",\"video_count\":" << current.header_summary()->video_count;
            }
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            out << ",\"container_filename\":" << quote_json(current.container_filename())
                << ",\"stream_count\":" << current.streams().size()
                << ",\"has_sfsh\":" << bool_text(current.sfsh_header().has_value())
                << ",\"crid_table\":" << quote_json(current.crid_header().table_name())
                << ",\"streams\":[";
            for (size_t index = 0; index < current.streams().size(); ++index) {
                if (index != 0) {
                    out << ',';
                }
                const auto& stream = current.streams()[index];
                out << "{\"index\":" << index
                    << ",\"type\":" << quote_json(usm_stream_type_text(stream.stream_id))
                    << ",\"channel\":" << static_cast<unsigned int>(stream.channel_no)
                    << ",\"filename\":" << quote_json(stream.filename)
                    << ",\"audio_codec\":";
                if (stream.audio_codec.has_value()) {
                    out << quote_json(usm::audio_codec_name(*stream.audio_codec));
                } else {
                    out << "null";
                }
                out << '}';
            }
            out << ']';
        } else if constexpr (std::same_as<T, utf::UtfTable>) {
            out << ",\"table_name\":" << quote_json(current.table_name())
                << ",\"version\":" << current.version()
                << ",\"row_count\":" << current.row_count()
                << ",\"column_count\":" << current.column_count()
                << ",\"table_size\":" << current.table_size()
                << ",\"row_width\":" << current.row_width()
                << ",\"columns\":[";
            for (uint32_t index = 0; index < current.column_count(); ++index) {
                if (index != 0) {
                    out << ',';
                }
                const auto& column = current.column(index);
                out << "{"
                    << "\"name\":" << quote_json(column.name)
                    << ",\"type\":" << static_cast<unsigned int>(column.type)
                    << ",\"flag_bits\":" << static_cast<unsigned int>(column.flag)
                    << "}";
            }
            out << "],";
            print_utf_rows_json(out, current);
        }

        out << '}';
    }, document);
}


} // namespace cricodecs::cli::detail
